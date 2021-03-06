#ifndef SCT_SYSTEMATICS_HISTOGRAM_COLLECTION_H
#define SCT_SYSTEMATICS_HISTOGRAM_COLLECTION_H

#include "sct/lib/map.h"
#include "sct/lib/memory.h"
#include "sct/lib/string/string.h"

#include <iostream>

namespace sct {
template <class H, class Key = std::string, class hash = std::hash<Key>>
class HistogramCollection {
 public:
  HistogramCollection() : histograms_(){};
  ~HistogramCollection(){};

  H* get(Key key) {
    if (keyExists(key)) return histograms_[key].get();
    return nullptr;
  }

  template <typename... Args>
  void add(Key key, Key title, Args... args) {
    histograms_[key] = make_unique<H>(key.c_str(), title.c_str(), args...);
    histograms_[key]->SetDirectory(0);
  }

  template <typename... Args>
  bool fill(Key key, Args... args) {
    if (!keyExists(key)) return false;
    histograms_[key]->Fill(args...);
    return true;
  }

  void write() {
    for (auto& h : histograms_) h.second->Write();
  }

  void clear() { histograms_.clear(); }

 private:
  sct_map<Key, unique_ptr<H>, hash> histograms_ = {};

  bool keyExists(string key) {
    for (auto& h : histograms_) {
      if (h.first == key) return true;
    }
    return false;
  }
};
}  // namespace sct

#endif  // SCT_SYSTEMATICS_HISTOGRAM_COLLECTION_H
