#include "destinations/DestinationRepository.h"

#include <Windows.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>

namespace {
std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");

    if (first == std::string::npos) {
        return {};
    }

    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

bool ParseRegion(const std::string &value, DestinationRegion &region) {
    if (value == "BEACH") {
        region = DestinationRegion::Beach;
    } else if (value == "STARFISH_ISLAND") {
        region = DestinationRegion::StarfishIsland;
    } else if (value == "MAINLAND") {
        region = DestinationRegion::Mainland;
    } else {
        return false;
    }

    return true;
}

DestinationIcon ParseIcon(std::string value) {
    value = Trim(std::move(value));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });

    using I = DestinationIcon;
    if (value == "lawyer") {
        return I::Lawyer;
    }
    if (value == "tshirt") {
        return I::Tshirt;
    }
    if (value == "boatyard") {
        return I::Boatyard;
    }
    if (value == "strip") {
        return I::Strip;
    }
    if (value == "gun") {
        return I::Gun;
    }
    if (value == "spray") {
        return I::Spray;
    }
    if (value == "save") {
        return I::Save;
    }
    if (value == "club") {
        return I::Club;
    }
    if (value == "avery") {
        return I::Avery;
    }
    if (value == "diaz_mansion") {
        return I::DiazMansion;
    }
    if (value == "mall") {
        return I::Mall;
    }
    if (value == "filmstudio") {
        return I::FilmStudio;
    }
    if (value == "vrock") {
        return I::Vrock;
    }
    if (value == "bikers") {
        return I::Bikers;
    }
    if (value == "printworks") {
        return I::Printworks;
    }
    if (value == "icecream") {
        return I::Icecream;
    }
    if (value == "kcabs") {
        return I::Kcabs;
    }
    if (value == "haitians") {
        return I::Haitians;
    }
    if (value == "phil") {
        return I::Phil;
    }
    if (value == "sunyard") {
        return I::Sunyard;
    }
    if (value == "cubans") {
        return I::Cubans;
    }
    return I::MapHere;
}

} // namespace

void DestinationRepository::Load(const std::filesystem::path &path) {
    destinations_.clear();
    std::ifstream file(path);
    std::string line;
    std::size_t lineNumber{};
    while (file && std::getline(file, line)) {
        ++lineNumber;
        line = Trim(line);

        if (line.empty() || line.front() == '#' || line.starts_with("name,")) {
            continue;
        }

        std::array<std::string, 7> fields;
        std::stringstream stream(line);
        bool complete = true;
        for (std::size_t i = 0; i < 6; ++i) {
            complete = complete && static_cast<bool>(std::getline(stream, fields[i], ','));
        }
        std::getline(stream, fields[6], ',');

        DestinationRegion region{};
        try {
            if (!complete || !ParseRegion(Trim(fields[1]), region)) {
                throw std::invalid_argument("row");
            }

            destinations_.push_back({Trim(fields[0]),
                                     region,
                                     {std::stof(Trim(fields[2])), std::stof(Trim(fields[3])),
                                      std::stof(Trim(fields[4]))},
                                     std::stof(Trim(fields[5])),
                                     ParseIcon(fields[6])});
        } catch (...) {
            const std::string message = "TaxiManagerVC: skipped invalid destination row " +
                                        std::to_string(lineNumber) + "\n";
            OutputDebugStringA(message.c_str());
        }
    }

    if (destinations_.empty()) {
        destinations_ = Defaults();
    }
}

std::vector<std::size_t> DestinationRepository::Available() const {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < destinations_.size(); ++i) {
        if (IsUnlocked(destinations_[i].island)) {
            result.push_back(i);
        }
    }

    return result;
}

bool DestinationRepository::IsUnlocked(DestinationRegion) {
    // Route validation occurs when a destination is confirmed. This avoids
    // depending on main.scm global indices, which differ between installations.
    return true;
}

std::vector<Destination> DestinationRepository::Defaults() {
    using R = DestinationRegion;
    using I = DestinationIcon;
    return {
        {"Ken Rosenburg's Office", R::Beach, {108.80F, -813.16F, 11.04F}, 2.55F, I::Lawyer},
        {"Rafael's", R::Beach, {105.17F, -1117.22F, 11.07F}, 4.69F, I::Tshirt},
        {"Ocean Bay Marina", R::Beach, {-166.64F, -1443.76F, 11.56F}, 3.50F, I::Boatyard},
        {"The Pole Position Club", R::Beach, {110.47F, -1493.32F, 9.91F}, 6.09F, I::Strip},
        {"Ammu-Nation Ocean Beach", R::Beach, {-53.93F, -1481.91F, 11.24F}, 3.00F, I::Gun},
        {"Pay n Spray Ocean Beach", R::Beach, {-18.68F, -1262.94F, 11.27F}, 0.00F, I::Spray},
        {"Ocean View Hotel", R::Beach, {240.62F, -1285.84F, 11.73F}, 2.89F, I::Save},
        {"Malibu Club", R::Beach, {493.96F, -97.75F, 11.13F}, 1.56F, I::Club},
        {"Avery Construction Site", R::Beach, {243.74F, -229.29F, 11.89F}, 2.60F, I::Avery},
        {"Diaz Mansion", R::StarfishIsland, {-281.09F, -469.86F, 11.95F}, 1.60F, I::DiazMansion},
        {"North Point Mall", R::Beach, {488.52F, 1122.98F, 17.23F}, 3.03F, I::Mall},
        {"InterGlobal Studios", R::Beach, {19.76F, 972.32F, 11.55F}, 3.00F, I::FilmStudio},
        {"Ammu-Nation Downtown", R::Mainland, {-659.21F, 1194.73F, 11.78F}, 1.50F, I::Gun},
        {"V-Rock Recording Studio", R::Mainland, {-863.79F, 1151.79F, 11.98F}, 3.00F, I::Vrock},
        {"Hyman Condo", R::Mainland, {-862.46F, 1291.98F, 12.35F}, 0.00F, I::Save},
        {"The Greasy Chopper Bar", R::Mainland, {-610.77F, 654.57F, 12.17F}, 4.79F, I::Bikers},
        {"El Banco Corrupto Grande",
         R::Mainland,
         {-869.55F, -341.29F, 11.84F},
         3.10F,
         I::Printworks},
        {"Print Works", R::Mainland, {-1042.33F, -272.42F, 12.11F}, 4.69F, I::Printworks},
        {"Cherry Popper Ice Cream Factory",
         R::Mainland,
         {-848.42F, -565.43F, 11.93F},
         3.25F,
         I::Icecream},
        {"Kaufman Cabs", R::Mainland, {-1014.52F, 210.39F, 12.02F}, 6.10F, I::Kcabs},
        {"Auntie Poulet House", R::Mainland, {-942.82F, 123.29F, 10.16F}, 3.06F, I::Haitians},
        {"Phil's Place", R::Mainland, {-998.42F, 307.88F, 12.14F}, 5.80F, I::Phil},
        {"Sunshine Autos", R::Mainland, {-1026.67F, -904.00F, 14.86F}, 7.00F, I::Sunyard},
        {"Cafe Robina", R::Mainland, {-1165.60F, -590.14F, 11.47F}, 1.70F, I::Cubans},
        {"Escobar International Airport",
         R::Mainland,
         {-1457.76F, -827.05F, 15.70F},
         3.94F,
         I::MapHere},
        {"Viceport Boatyard", R::Mainland, {-697.59F, -1485.25F, 11.88F}, 5.78F, I::Boatyard}};
}
