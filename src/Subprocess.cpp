// ekam -- http://code.google.com/p/ekam
// Copyright (c) 2010 Kenton Varda and contributors.  All rights reserved.
// Portions copyright Google, Inc.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
//     * Neither the name of the ekam project nor the names of its
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "Subprocess.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>

#include "Debug.h"

namespace ekam {

class Subprocess::CallbackWrapper : public EventManager::ProcessExitCallback {
public:
  CallbackWrapper(Subprocess* subprocess)
      : subprocess(subprocess) {}
  ~CallbackWrapper() {}

  // implements ProcessExitCallback ------------------------------------------------------

  void exited(int exitCode) {
    subprocess->done(EXITED, exitCode);
  }

  void signaled(int signalNumber) {
    subprocess->done(SIGNALED, signalNumber);
  }

private:
  Subprocess* subprocess;
};

// =======================================================================================

Subprocess::Subprocess() : doPathLookup(false), pipeCount(0), pid(-1), state(NOT_STARTED) {}

Subprocess::~Subprocess() {
  if (canceler != NULL) {
    canceler->cancel();
    kill(pid, SIGKILL);
    int dummy;
    waitpid(pid, &dummy, 0);
  }
}

void Subprocess::addArgument(const std::string& arg) {
  if (args.empty()) {
    executableName = arg;
    doPathLookup = true;
  }
  args.push_back(arg);
}

void Subprocess::addArgument(File* file, File::Usage usage) {
  OwnedPtr<File::DiskRef> diskRef;
  file->getOnDisk(usage, &diskRef);

  if (args.empty()) {
    executableName = diskRef->path();
    doPathLookup = false;
  }
  args.push_back(diskRef->path());

  diskRefs.adoptBack(&diskRef);
}

void Subprocess::captureStdout(OwnedPtr<FileDescriptor>* output) {
  stdoutPipe.allocate();
  stdoutPipe->releaseReadEnd(output);
  stdoutAndStderrPipe.clear();
}

void Subprocess::captureStderr(OwnedPtr<FileDescriptor>* output) {
  stderrPipe.allocate();
  stderrPipe->releaseReadEnd(output);
  stdoutAndStderrPipe.clear();
}

void Subprocess::captureStdoutAndStderr(OwnedPtr<FileDescriptor>* output) {
  stdoutAndStderrPipe.allocate();
  stdoutAndStderrPipe->releaseReadEnd(output);
  stdoutPipe.clear();
  stderrPipe.clear();
}

void Subprocess::start(EventManager* eventManager,
                       OwnedPtr<EventManager::ProcessExitCallback>* callbackToAdopt) {
  pid = fork();

  if (pid < 0) {
    throw OsError("", "fork", errno);
  } else if (pid == 0) {
    // In child.

    if (stdoutPipe != NULL) {
      stdoutPipe->attachWriteEndForExec(STDOUT_FILENO);
    }
    if (stderrPipe != NULL) {
      stderrPipe->attachWriteEndForExec(STDERR_FILENO);
    }
    if (stdoutAndStderrPipe != NULL) {
      stdoutAndStderrPipe->attachWriteEndForExec(STDOUT_FILENO);
      dup2(STDOUT_FILENO, STDERR_FILENO);
    }

    std::vector<char*> argv;
    std::string command;

    for (unsigned int i = 0; i < args.size(); i++) {
      argv.push_back(strdup(args[i].c_str()));

      if (i > 0) command.push_back(' ');
      command.append(args[i]);
    }

    argv.push_back(NULL);

    DEBUG_INFO << "exec: " << command;

    if (doPathLookup) {
      execvp(executableName.c_str(), &argv[0]);
    } else {
      execv(executableName.c_str(), &argv[0]);
    }

    perror("exec");
    exit(1);
  } else {
    state = RUNNING;
    if (stdoutPipe != NULL) {
      stdoutPipe.clear();
      ++pipeCount;
    }
    if (stderrPipe != NULL) {
      stderrPipe.clear();
      ++pipeCount;
    }
    if (stdoutAndStderrPipe != NULL) {
      stdoutAndStderrPipe.clear();
      ++pipeCount;
    }

    finalCallback.adopt(callbackToAdopt);

    OwnedPtr<EventManager::ProcessExitCallback> callback;
    callback.allocateSubclass<CallbackWrapper>(this);
    eventManager->onProcessExit(pid, &callback, &canceler);
  }
}

void Subprocess::pipeDone() {
  --pipeCount;
  maybeCallFinalCallback();
}

void Subprocess::done(State state, int exitStatusOrSignalNumber) {
  this->state = state;
  this->exitStatusOrSignalNumber = exitStatusOrSignalNumber;
  canceler.clear();
  pid = -1;
  diskRefs.clear();

  maybeCallFinalCallback();
}

void Subprocess::maybeCallFinalCallback() {
  if (pipeCount == 0) {
    if (state == EXITED) {
      finalCallback->exited(exitStatusOrSignalNumber);
      finalCallback.clear();  // Warning:  May delete this.
    } else if (state == SIGNALED) {
      finalCallback->signaled(exitStatusOrSignalNumber);
      finalCallback.clear();  // Warning:  May delete this.
    }
  }
}

}  // namespace ekam