#pragma once

#include "core/TaxiTypes.h"

#include <filesystem>
#include <vector>

class DestinationRepository final {
  public:
    void Load(const std::filesystem::path &path);
    const std::vector<Destination> &All() const noexcept {
        return destinations_;
    }

    std::vector<std::size_t> Available() const;
    static bool IsUnlocked(DestinationRegion island);

  private:
    std::vector<Destination> destinations_;
    static std::vector<Destination> Defaults();
};
