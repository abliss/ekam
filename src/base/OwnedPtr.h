// Ekam Build System
// Author: Kenton Varda (kenton@sandstorm.io)
// Copyright (c) 2010-2015 Kenton Varda, Google Inc., and contributors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef KENTONSCODE_BASE_OWNEDPTR_H_
#define KENTONSCODE_BASE_OWNEDPTR_H_

#include <stddef.h>
#include <type_traits>
#include <vector>
#include <deque>
#include <queue>
#include <unordered_map>
#include <assert.h>

#ifdef __CDT_PARSER__
#define noexcept
#define constexpr
namespace std { struct nullptr_t; }
#endif

namespace ekam {

template <typename T>
inline void deleteEnsuringCompleteType(T* ptr) {
  enum { type_must_be_complete = sizeof(T) };
  delete ptr;
}

template <typename T>
class OwnedPtr {
public:
  OwnedPtr() : ptr(NULL) {}
  OwnedPtr(const OwnedPtr&) = delete;
  OwnedPtr(OwnedPtr&& other) : ptr(other.releaseRaw()) {}
  template <typename U>
  OwnedPtr(OwnedPtr<U>&& other) : ptr(other.releaseRaw()) {}
  OwnedPtr(std::nullptr_t) : ptr(NULL) {}
  ~OwnedPtr() {
    deleteEnsuringCompleteType(ptr);
  }

  OwnedPtr& operator=(const OwnedPtr&) = delete;
  OwnedPtr& operator=(OwnedPtr&& other) {
    reset(other.releaseRaw());
    return *this;
  }

  template <typename U>
  OwnedPtr& operator=(OwnedPtr<U>&& other) {
    reset(other.releaseRaw());
    return *this;
  }

  T* get() const { return ptr; }
  T* operator->() const { assert(ptr != NULL); return ptr; }
  T& operator*() const { assert(ptr != NULL); return *ptr; }

  OwnedPtr release() {
    return OwnedPtr(releaseRaw());
  }

  void clear() {
    reset(NULL);
  }

  bool operator==(const T* other) { return ptr == other; }
  bool operator!=(const T* other) { return ptr != other; }

private:
  T* ptr;

  explicit OwnedPtr(T* ptr) : ptr(ptr) {}

  void reset(T* newValue) {
    T* oldValue = ptr;
    ptr = newValue;
    deleteEnsuringCompleteType(oldValue);
  }

  T* releaseRaw() {
    T* result = ptr;
    ptr = NULL;
    return result;
  }

  template <typename U>
  friend class OwnedPtr;
  template <typename U, typename... Params>
  friend OwnedPtr<U> newOwned(Params&&... params);
  template <typename U>
  friend class SmartPtr;
  template <typename U>
  friend class OwnedPtrVector;
  template <typename U>
  friend class OwnedPtrDeque;
  template <typename U>
  friend class OwnedPtrQueue;
  template <typename Key, typename U, typename HashFunc, typename EqualsFunc>
  friend class OwnedPtrMap;
};

template <typename T, typename... Params>
OwnedPtr<T> newOwned(Params&&... params) {
  return OwnedPtr<T>(new T(std::forward<Params>(params)...));
}

template <typename T>
class Indirect {
public:
  template <typename... Params>
  Indirect(Params&&... params): ptr(newOwned<T>(std::forward<Params>(params)...)) {}
  Indirect(Indirect&& other): ptr(other.ptr.release()) {}
  Indirect(const Indirect& other): ptr(newOwned<T>(*other.ptr)) {}

  Indirect& operator=(Indirect&& other) { ptr = other.ptr.release(); return *this; }
  Indirect& operator=(const Indirect& other) { ptr = newOwned<T>(*other.ptr); return *this; }

  bool operator==(const Indirect& other) const { return *ptr == *other.ptr; }
  bool operator!=(const Indirect& other) const { return *ptr != *other.ptr; }

  const T& operator*() const { return *ptr; }
  T& operator*() { return *ptr; }

  const T* operator->() const { return ptr.get(); }
  T* operator->() { return ptr.get(); }

private:
  OwnedPtr<T> ptr;
};

// TODO:  Hide this somewhere private?
class Refcount {
public:
  Refcount(): strong(1), weak(0) {}
  Refcount(const Refcount& other) = delete;
  Refcount& operator=(const Refcount& other) = delete;

  static void inc(Refcount* r) {
    if (r != NULL) ++r->strong;
  }
  static bool dec(Refcount* r) {
    if (r == NULL) {
      return false;
    } else if (--r->strong == 0) {
      if (r->weak == 0) {
        delete r;
      }
      return true;
    } else {
      return false;
    }
  }

  static void incWeak(Refcount* r) {
    if (r != NULL) ++r->weak;
  }
  static void decWeak(Refcount* r) {
    if (r != NULL && --r->weak == 0 && r->strong == 0) {
      delete r;
    }
  }

  static bool release(Refcount* r) {
    if (r != NULL && r->strong == 1) {
      dec(r);
      return true;
    } else {
      return false;
    }
  }

  static bool isLive(Refcount* r) {
    return r != NULL && r->strong > 0;
  }

private:
  int strong;
  int weak;
};

template <typename T>
class SmartPtr {
public:
  SmartPtr() : ptr(NULL), refcount(NULL) {}
  SmartPtr(std::nullptr_t) : ptr(NULL), refcount(NULL) {}
  ~SmartPtr() {
    if (Refcount::dec(refcount)) {
      deleteEnsuringCompleteType(ptr);
    }
  }

  SmartPtr(const SmartPtr& other) : ptr(other.ptr), refcount(other.refcount) {
    Refcount::inc(refcount);
  }
  template <typename U>
  SmartPtr(const SmartPtr<U>& other) : ptr(other.ptr), refcount(other.refcount) {
    Refcount::inc(refcount);
  }
  SmartPtr& operator=(const SmartPtr& other) {
    reset(other.ptr, other.refcount);
    return *this;
  }
  template <typename U>
  SmartPtr& operator=(const SmartPtr<U>& other) {
    reset(other.ptr, other.refcount);
    return *this;
  }

  SmartPtr(SmartPtr&& other) : ptr(other.ptr), refcount(other.refcount) {
    other.ptr = NULL;
    other.refcount = NULL;
  }
  template <typename U>
  SmartPtr(SmartPtr<U>&& other) : ptr(other.ptr), refcount(other.refcount) {
    other.ptr = NULL;
    other.refcount = NULL;
  }
  SmartPtr& operator=(SmartPtr&& other) {
    // Move pointers to locals before reset() in case &other == this.
    T* tempPtr = other.ptr;
    Refcount* tempRefcount = other.refcount;
    other.ptr = NULL;
    other.refcount = NULL;

    reset(NULL, NULL);

    ptr = tempPtr;
    refcount = tempRefcount;
    return *this;
  }
  template <typename U>
  SmartPtr& operator=(SmartPtr<U>&& other) {
    // Move pointers to locals before reset() in case &other == this.
    T* tempPtr = other.ptr;
    Refcount* tempRefcount = other.refcount;
    other.ptr = NULL;
    other.refcount = NULL;

    reset(NULL, NULL);

    ptr = tempPtr;
    refcount = tempRefcount;
    return *this;
  }

  template <typename U>
  SmartPtr(OwnedPtr<U>&& other)
      : ptr(other.releaseRaw()),
        refcount(ptr == NULL ? NULL : new Refcount()) {}
  template <typename U>
  SmartPtr& operator=(OwnedPtr<U>&& other) {
    reset(other.releaseRaw());
    return *this;
  }

  T* get() const { return ptr; }
  T* operator->() const { assert(ptr != NULL); return ptr; }
  T& operator*() const { assert(ptr != NULL); return *ptr; }

  template <typename U>
  bool release(OwnedPtr<U>* other) {
    if (Refcount::release(refcount)) {
      other->reset(ptr);
      ptr = NULL;
      return true;
    } else {
      return false;
    }
  }

  void clear() {
    reset(NULL);
  }

  bool operator==(const T* other) { return ptr == other; }
  bool operator!=(const T* other) { return ptr != other; }
  bool operator==(const SmartPtr<T>& other) { return ptr == other.ptr; }
  bool operator!=(const SmartPtr<T>& other) { return ptr != other.ptr; }

  void allocate() {
    reset(new T());
  }
  template <typename P1>
  void allocate(const P1& p1) {
    reset(new T(p1));
  }
  template <typename P1, typename P2>
  void allocate(const P1& p1, const P2& p2) {
    reset(new T(p1, p2));
  }
  template <typename P1, typename P2, typename P3>
  void allocate(const P1& p1, const P2& p2, const P3& p3) {
    reset(new T(p1, p2, p3));
  }
  template <typename P1, typename P2, typename P3, typename P4>
  void allocate(const P1& p1, const P2& p2, const P3& p3, const P4& p4) {
    reset(new T(p1, p2, p3, p4));
  }

  template <typename Sub>
  void allocateSubclass() {
    reset(new Sub());
  }
  template <typename Sub, typename P1>
  void allocateSubclass(const P1& p1) {
    reset(new Sub(p1));
  }
  template <typename Sub, typename P1, typename P2>
  void allocateSubclass(const P1& p1, const P2& p2) {
    reset(new Sub(p1, p2));
  }
  template <typename Sub, typename P1, typename P2, typename P3>
  void allocateSubclass(const P1& p1, const P2& p2, const P3& p3) {
    reset(new Sub(p1, p2, p3));
  }
  template <typename Sub, typename P1, typename P2, typename P3, typename P4>
  void allocateSubclass(const P1& p1, const P2& p2, const P3& p3, const P4& p4) {
    reset(new Sub(p1, p2, p3, p4));
  }

private:
  T* ptr;
  Refcount* refcount;

  inline void reset(T* newValue) {
    reset(newValue, newValue == NULL ? NULL : new Refcount());
    Refcount::dec(refcount);
  }

  void reset(T* newValue, Refcount* newRefcount) {
    T* oldValue = ptr;
    Refcount* oldRefcount = refcount;
    ptr = newValue;
    refcount = newRefcount;
    Refcount::inc(refcount);
    if (Refcount::dec(oldRefcount)) {
      deleteEnsuringCompleteType(oldValue);
    }
  }

  template <typename U>
  friend class SmartPtr;
  template <typename U>
  friend class WeakPtr;
  template <typename U>
  friend class OwnedPtr;
  template <typename U>
  friend class OwnedPtrVector;
  template <typename U>
  friend class OwnedPtrQueue;
  template <typename Key, typename U, typename HashFunc, typename EqualsFunc>
  friend class OwnedPtrMap;
};

template <typename T>
class WeakPtr {
public:
  WeakPtr(): ptr(NULL), refcount(NULL) {}
  WeakPtr(const WeakPtr& other): ptr(other.ptr), refcount(other.refcount) {
    Refcount::incWeak(refcount);
  }
  WeakPtr(const SmartPtr<T>& other): ptr(other.ptr), refcount(other.refcount) {
    Refcount::incWeak(refcount);
  }
  WeakPtr(std::nullptr_t): ptr(nullptr), refcount(nullptr) {}
  ~WeakPtr() {
    Refcount::decWeak(refcount);
  }

  WeakPtr& operator=(const WeakPtr& other) {
    Refcount* oldRefcount = refcount;
    ptr = other.ptr;
    refcount = other.refcount;
    Refcount::incWeak(refcount);
    Refcount::decWeak(oldRefcount);
    return *this;
  }
  template <typename U>
  WeakPtr& operator=(const WeakPtr<U>& other) {
    Refcount* oldRefcount = refcount;
    ptr = other.ptr;
    refcount = other.refcount;
    Refcount::incWeak(refcount);
    Refcount::decWeak(oldRefcount);
    return *this;
  }
  template <typename U>
  WeakPtr& operator=(const SmartPtr<U>& other) {
    Refcount* oldRefcount = refcount;
    ptr = other.ptr;
    refcount = other.refcount;
    Refcount::incWeak(refcount);
    Refcount::decWeak(oldRefcount);
    return *this;
  }
  WeakPtr& operator=(std::nullptr_t) {
    Refcount::decWeak(refcount);
    ptr = nullptr;
    refcount = nullptr;
    return *this;
  }

  template <typename U>
  operator SmartPtr<U>() const {
    SmartPtr<U> result;
    if (Refcount::isLive(refcount)) {
      result.reset(ptr, refcount);
    }
    return result;
  }

private:
  T* ptr;
  Refcount* refcount;
};

template <typename T>
class OwnedPtrVector {
public:
  OwnedPtrVector() {}
  OwnedPtrVector(const OwnedPtrVector&) = delete;
  OwnedPtrVector(OwnedPtrVector&& other) {
    vec.swap(other.vec);
  }
  ~OwnedPtrVector() {
    for (typename std::vector<T*>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
  }

  OwnedPtrVector& operator=(const OwnedPtrVector&) = delete;

  int size() const { return vec.size(); }
  T* get(int index) const { return vec[index]; }
  bool empty() const { return vec.empty(); }

  void add(OwnedPtr<T> ptr) {
    vec.push_back(ptr.releaseRaw());
  }

  void set(int index, OwnedPtr<T> ptr) {
    deleteEnsuringCompleteType(vec[index]);
    vec[index] = ptr->releaseRaw();
  }

  OwnedPtr<T> release(int index) {
    T* result = vec[index];
    vec[index] = NULL;
    return OwnedPtr<T>(result);
  }

  OwnedPtr<T> releaseBack() {
    T* result = vec.back();
    vec.pop_back();
    return OwnedPtr<T>(result);
  }

  OwnedPtr<T> releaseAndShift(int index) {
    T* result = vec[index];
    vec.erase(vec.begin() + index);
    return OwnedPtr<T>(result);
  }

  void clear() {
    for (typename std::vector<T*>::const_iterator iter = vec.begin(); iter != vec.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
    vec.clear();
  }

  void swap(OwnedPtrVector* other) {
    vec.swap(other->vec);
  }

  class Appender {
  public:
    explicit Appender(OwnedPtrVector* vec) : vec(vec) {}

    void add(OwnedPtr<T> ptr) {
      vec->add(ptr.release());
    }

  private:
    OwnedPtrVector* vec;
  };

  Appender appender() {
    return Appender(this);
  }

private:
  std::vector<T*> vec;
};

template <typename T>
class OwnedPtrDeque {
public:
  OwnedPtrDeque() {}
  ~OwnedPtrDeque() {
    for (typename std::deque<T*>::const_iterator iter = q.begin(); iter != q.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
  }

  int size() const { return q.size(); }
  T* get(int index) const { return q[index]; }
  bool empty() const { return q.empty(); }

  void pushFront(OwnedPtr<T> ptr) {
    q.push_front(ptr.releaseRaw());
  }

  OwnedPtr<T> popFront() {
    T* ptr = q.front();
    q.pop_front();
    return OwnedPtr<T>(ptr);
  }

  void pushBack(OwnedPtr<T> ptr) {
    q.push_back(ptr.releaseRaw());
  }

  OwnedPtr<T> popBack() {
    T* ptr = q.back();
    q.pop_back();
    return OwnedPtr<T>(ptr);
  }

  OwnedPtr<T> releaseAndShift(int index) {
    T* ptr = q[index];
    q.erase(q.begin() + index);
    return OwnedPtr<T>(ptr);
  }

  void clear() {
    for (typename std::deque<T*>::const_iterator iter = q.begin(); iter != q.end(); ++iter) {
      deleteEnsuringCompleteType(*iter);
    }
    q.clear();
  }

  void swap(OwnedPtrDeque* other) {
    q.swap(other->q);
  }

private:
  std::deque<T*> q;
};

template <typename T>
class OwnedPtrQueue {
public:
  OwnedPtrQueue() {}
  ~OwnedPtrQueue() {
    clear();
  }

  int size() const { return q.size(); }
  bool empty() const { return q.empty(); }

  void push(OwnedPtr<T> ptr) {
    q.push(ptr.releaseRaw());
  }

  OwnedPtr<T> pop() {
    T* ptr = q.front();
    q.pop();
    return OwnedPtr<T>(ptr);
  }

  void clear() {
    while (!q.empty()) {
      deleteEnsuringCompleteType(q.front());
      q.pop();
    }
  }

  class Appender {
  public:
    Appender(OwnedPtrQueue* q) : q(q) {}

    void add(OwnedPtr<T> ptr) {
      q->push(ptr);
    }

  private:
    OwnedPtrQueue* q;
  };

  Appender appender() {
    return Appender(this);
  }

private:
  std::queue<T*> q;
};

template <typename Key, typename T,
          typename HashFunc = std::hash<Key>,
          typename EqualsFunc = std::equal_to<Key> >
class OwnedPtrMap {
  typedef std::unordered_map<Key, T*, HashFunc, EqualsFunc> InnerMap;

public:
  OwnedPtrMap() {}
  ~OwnedPtrMap() {
    for (typename InnerMap::const_iterator iter = map.begin();
         iter != map.end(); ++iter) {
      deleteEnsuringCompleteType(iter->second);
    }
  }

  bool empty() const {
    return map.empty();
  }

  int size() const {
    return map.size();
  }

  bool contains(const Key& key) const {
    return map.count(key) > 0;
  }

  T* get(const Key& key) const {
    typename InnerMap::const_iterator iter = map.find(key);
    if (iter == map.end()) {
      return NULL;
    } else {
      return iter->second;
    }
  }

  void add(const Key& key, OwnedPtr<T> ptr) {
    T* value = ptr.releaseRaw();
    std::pair<typename InnerMap::iterator, bool> insertResult =
        map.insert(std::make_pair(key, value));
    if (!insertResult.second) {
      deleteEnsuringCompleteType(insertResult.first->second);
      insertResult.first->second = value;
    }
  }

  bool addIfNew(const Key& key, OwnedPtr<T> ptr) {
    T* value = ptr.releaseRaw();
    std::pair<typename InnerMap::iterator, bool> insertResult =
        map.insert(std::make_pair(key, value));
    if (insertResult.second) {
      return true;
    } else {
      deleteEnsuringCompleteType(value);
      return false;
    }
  }

  bool release(const Key& key, OwnedPtr<T>* output) {
    typename InnerMap::iterator iter = map.find(key);
    if (iter == map.end()) {
      output->reset(NULL);
      return false;
    } else {
      output->reset(iter->second);
      map.erase(iter);
      return true;
    }
  }

  void releaseAll(typename OwnedPtrVector<T>::Appender output) {
    for (typename InnerMap::const_iterator iter = map.begin();
         iter != map.end(); ++iter) {
      output.add(OwnedPtr<T>(iter->second));
    }
    map.clear();
  }

  bool erase(const Key& key) {
    typename InnerMap::iterator iter = map.find(key);
    if (iter == map.end()) {
      return false;
    } else {
      deleteEnsuringCompleteType(iter->second);
      map.erase(iter);
      return true;
    }
  }

  void clear() {
    for (typename InnerMap::const_iterator iter = map.begin();
         iter != map.end(); ++iter) {
      deleteEnsuringCompleteType(iter->second);
    }
    map.clear();
  }

  void swap(OwnedPtrMap* other) {
    map.swap(other->map);
  }

  class Iterator {
  public:
    Iterator(const OwnedPtrMap& map)
      : nextIter(map.map.begin()),
        end(map.map.end()) {}

    bool next() {
      if (nextIter == end) {
        return false;
      } else {
        iter = nextIter;
        ++nextIter;
        return true;
      }
    }

    const Key& key() {
      return iter->first;
    }

    T* value() {
      return iter->second;
    }

  private:
    typename InnerMap::const_iterator iter;
    typename InnerMap::const_iterator nextIter;
    typename InnerMap::const_iterator end;
  };

private:
  InnerMap map;
};

}  // namespace ekam

#endif  // KENTONSCODE_BASE_OWNEDPTR_H_
