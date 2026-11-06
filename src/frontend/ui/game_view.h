#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace emu::frontend::ui {

bool fuzzyMatch(const std::string& haystack, const std::string& needle,
                int* score);

std::string foldCase(const std::string& text);

enum class SystemId { Unknown, Nes, GameBoy, Chip8, Ps1, Psp, Ps2 };
SystemId systemFromPath(const std::string& path);
const char* systemShortName(SystemId system);

template <typename Entry>
class GameViewT {
public:
    enum class Filter { All, Favorites, Recent, System };
    enum class Sort { Title, Year, System };

    void setSource(const std::vector<Entry>* games) {
        games_ = games;
        folded_.clear();
        dirty_ = true;
    }

    void setQuery(std::string query) {
        if (query == query_) return;
        query_ = std::move(query);
        query_folded_ = foldCase(query_);
        dirty_ = true;
    }
    const std::string& query() const { return query_; }

    void setFilter(Filter filter) {
        if (filter == filter_) return;
        filter_ = filter;
        dirty_ = true;
    }
    Filter filter() const { return filter_; }

    void setSort(Sort sort) {
        if (sort == sort_) return;
        sort_ = sort;
        dirty_ = true;
    }

    void setSystem(SystemId system) {
        if (system == system_) return;
        system_ = system;
        dirty_ = true;
    }

    void invalidate() { dirty_ = true; }

    bool refresh();
    
    std::size_t size() const { return order_.size(); }
    bool empty() const { return order_.empty(); }
    const Entry& at(std::size_t index) const { return (*games_)[order_[index]]; }
    std::size_t sourceIndex(std::size_t index) const { return order_[index]; }

    long long indexOfPath(const std::string& path_key) const;

private:
    bool lessBySort(const Entry& a, const Entry& b) const;
    const std::string& foldedTitle(std::size_t index) const;

    const std::vector<Entry>* games_ = nullptr;
    std::vector<std::size_t> order_;
    mutable std::vector<std::string> folded_;
    std::string query_;
    std::string query_folded_;
    Filter filter_ = Filter::All;
    Sort sort_ = Sort::Title;
    SystemId system_ = SystemId::Unknown;
    bool dirty_ = true;
};


template <typename Entry>
const std::string& GameViewT<Entry>::foldedTitle(std::size_t index) const {
    if (folded_.size() != games_->size()) {
        folded_.clear();
        folded_.reserve(games_->size());
        for (const Entry& game : *games_) {
            folded_.push_back(foldCase(game.title));
        }
    }
    return folded_[index];
}

template <typename Entry>
bool GameViewT<Entry>::lessBySort(const Entry& a, const Entry& b) const {
    switch (sort_) {
    case Sort::Year:
        if (a.year != b.year) return a.year > b.year;
        break;
    case Sort::System: {
        const auto left = systemFromPath(a.path.string());
        const auto right = systemFromPath(b.path.string());
        if (left != right) return left < right;
        break;
    }
    case Sort::Title:
    default:
        break;
    }
    return foldCase(a.title) < foldCase(b.title);
}

template <typename Entry>
bool GameViewT<Entry>::refresh() {
    if (!dirty_ || !games_) return false;
    dirty_ = false;

    std::vector<std::pair<int, std::size_t>> scored;
    scored.reserve(games_->size());

    for (std::size_t i = 0; i < games_->size(); ++i) {
        const Entry& game = (*games_)[i];

        if (filter_ == Filter::Favorites && !game.favorite) continue;
        if (filter_ == Filter::System &&
            systemFromPath(game.path.string()) != system_) {
            continue;
        }

        int score = 0;
        if (!query_folded_.empty() &&
            !fuzzyMatch(foldedTitle(i), query_folded_, &score)) {
            continue;
        }
        scored.emplace_back(score, i);
    }

    if (query_folded_.empty()) {
        std::sort(scored.begin(), scored.end(),
                  [this](const auto& left, const auto& right) {
                      return lessBySort((*games_)[left.second],
                                        (*games_)[right.second]);
                  });
    } else {
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto& left, const auto& right) {
                             return left.first > right.first;
                         });
    }

    order_.clear();
    order_.reserve(scored.size());
    for (const auto& entry : scored) order_.push_back(entry.second);
    return true;
}

template <typename Entry>
long long GameViewT<Entry>::indexOfPath(const std::string& path_key) const {
    for (std::size_t i = 0; i < order_.size(); ++i) {
        if ((*games_)[order_[i]].path.string() == path_key) {
            return static_cast<long long>(i);
        }
    }
    return -1;
}

}