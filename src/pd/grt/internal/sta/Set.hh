// Set.hh: EOS-owned compatibility shim, NOT an upstream file.
// The in-tree STA module has no sta::Set class (the pin-era OpenSTA
// header of this name is GPL-3.0 and is deliberately not imported).
// GlobalRouter.cpp includes "sta/Set.hh" but never uses sta::Set, so
// this shim only has to provide the pin-era API surface for
// compilation. It is written fresh in EOS style: a std::set wrapper
// with the lookup and Java-style iterator helpers the pin-era
// callers use. If a future grt translation unit starts using
// sta::Set in earnest, extend this shim's surface then; do not
// import the upstream header.
#pragma once

#include <set>

namespace sta {

template <class KEY, class CMP = std::less<KEY>>
class Set : public std::set<KEY, CMP>
{
 public:
  Set() = default;
  explicit Set(const CMP& cmp) : std::set<KEY, CMP>(cmp) {}

  // Returns the stored key equal to key, or a null key when absent.
  // (Only meaningful for pointer keys, matching the pin-era use.)
  KEY findKey(const KEY key) const
  {
    auto it = this->find(key);
    return it != this->end() ? *it : nullptr;
  }

  bool hasKey(const KEY key) const { return this->find(key) != this->end(); }

  void insertSet(const std::set<KEY, CMP>* other)
  {
    if (other != nullptr) {
      this->insert(other->begin(), other->end());
    }
  }

  bool isSubset(const std::set<KEY, CMP>* other) const
  {
    if (other == nullptr) {
      return true;
    }
    for (const KEY& key : *other) {
      if (this->find(key) == this->end()) {
        return false;
      }
    }
    return true;
  }

  // Java-style iterator over the set, in ascending key order.
  class Iterator
  {
   public:
    Iterator() = default;
    explicit Iterator(std::set<KEY, CMP>* container) : container_(container)
    {
      if (container_ != nullptr) {
        iter_ = container_->begin();
      }
    }
    explicit Iterator(std::set<KEY, CMP>& container) : container_(&container)
    {
      iter_ = container_->begin();
    }
    void init(std::set<KEY, CMP>* container)
    {
      container_ = container;
      if (container_ != nullptr) {
        iter_ = container_->begin();
      }
    }
    bool hasNext() const
    {
      return container_ != nullptr && iter_ != container_->end();
    }
    KEY next() { return *iter_++; }

   private:
    std::set<KEY, CMP>* container_ = nullptr;
    typename std::set<KEY, CMP>::iterator iter_;
  };

  class ConstIterator
  {
   public:
    ConstIterator() = default;
    explicit ConstIterator(const std::set<KEY, CMP>* container)
        : container_(container)
    {
      if (container_ != nullptr) {
        iter_ = container_->begin();
      }
    }
    explicit ConstIterator(const std::set<KEY, CMP>& container)
        : container_(&container)
    {
      iter_ = container_->begin();
    }
    void init(const std::set<KEY, CMP>* container)
    {
      container_ = container;
      if (container_ != nullptr) {
        iter_ = container_->begin();
      }
    }
    bool hasNext() const
    {
      return container_ != nullptr && iter_ != container_->end();
    }
    KEY next() { return *iter_++; }

   private:
    const std::set<KEY, CMP>* container_ = nullptr;
    typename std::set<KEY, CMP>::const_iterator iter_;
  };
};

}  // namespace sta
