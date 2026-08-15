#include "ui/Hud.h"

#include <CFileLoader.h>
#include <CFont.h>
#include <CHud.h>
#include <CRadar.h>
#include <CRect.h>
#include <CSprite2d.h>
#include <CTimer.h>
#include <common.h>
#include <plugin.h>

#include <algorithm>
#include <vector>

void Hud::ShowSelection(std::string text, CRGBA color, unsigned int durationMs) {
    selection_ = std::move(text);
    selectionCategory_.clear();
    selectionColor_ = color;
    showSelection_ = true;
    showSelectionIcon_ = false;
    selectionExpiresAt_ = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds) + durationMs;
}

void Hud::ShowDestinationSelection(std::string category, std::string destination,
                                   DestinationIcon icon, CRGBA color,
                                   unsigned int durationMs) {
    selectionCategory_ = std::move(category);
    selection_ = std::move(destination);
    selectionIcon_ = icon;
    selectionColor_ = color;
    showSelection_ = true;
    showSelectionIcon_ = true;
    selectionExpiresAt_ = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds) + durationMs;
}

void Hud::ShowFare(int fare) {
    fare_ = "Cab fare $" + std::to_string(fare);
    showFare_ = true;
    fareExpiresAt_ = CTimer::m_snTimeInMilliseconds + 5000U;
}

void Hud::ShowHelp(std::string text) {
    help_ = std::move(text);
    // Let main.scm display and consume its built-in taxi-driver hint first.
    // Updating CHud every frame corrupts/restarts the help text, so our message
    // is deliberately submitted once after a short delay.
    helpShowAt_ = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds) + 500U;
    helpPending_ = true;
}

void Hud::HideHelp() noexcept {
    help_.clear();
    helpShowAt_ = 0;
    helpPending_ = false;
    Help("", true);
}

void Hud::ShowBrowser(std::string category, std::vector<DestinationBrowserRow> destinations,
                      std::size_t selectedIndex) {
    browserCategory_ = std::move(category);
    browserDestinations_ = std::move(destinations);
    browserSelectedIndex_ = selectedIndex;
    showBrowser_ = true;
}

void Hud::InitializeTextures(const std::filesystem::path &moduleDirectory) {
    ShutdownTextures();

    const auto mallIconPath = moduleDirectory / "northpointmallradaricon.txd";
    mallDictionary_ = CFileLoader::LoadTexDictionary(mallIconPath.string().c_str());

    if (mallDictionary_) {
        mallSprite_.m_pTexture = GetFirstTexture(mallDictionary_);
    }
}

void Hud::ShutdownTextures() noexcept {
    mallSprite_.m_pTexture = nullptr;

    if (mallDictionary_) {
        RwTexDictionaryDestroy(mallDictionary_);
        mallDictionary_ = nullptr;
    }
}

void Hud::UpdateHelp() {
    if (helpPending_ && static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds) >= helpShowAt_) {
        Help(help_, false);
        helpPending_ = false;
    }
}

void Hud::Reset() noexcept {
    showSelection_ = false;
    showFare_ = false;
    selection_.clear();
    selectionCategory_.clear();
    showSelectionIcon_ = false;
    fare_.clear();
    help_.clear();
    helpShowAt_ = 0;
    helpPending_ = false;
    showBrowser_ = false;
    browserCategory_.clear();
    browserDestinations_.clear();
}

void Hud::Draw(int fontStyle) {
    // TestMenu verifies VC's actual safe enum: Rage/Gothic, Subtitles,
    // Pricedown. Earlier code incorrectly treated 2 as Menu and used invalid 3.
    fontStyle = std::clamp(fontStyle, 0, 2);

    DrawStatusMessages(fontStyle);
    DrawDestinationBrowser(fontStyle);
}

void Hud::DrawText(const std::string &text, float x, float y, CRGBA color, int fontStyle,
                   bool centred) {
    CFont::SetBackgroundOff();
    CFont::SetScale(SCREEN_MULTIPLIER(0.75F), SCREEN_MULTIPLIER(1.25F));
    CFont::SetPropOn();
    CFont::SetDropShadowPosition(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetFontStyle(fontStyle);
    CFont::SetColor(color);

    if (centred) {
        CFont::SetJustifyOff();
        CFont::SetCentreOn();
        CFont::SetCentreSize(SCREEN_WIDTH);
    } else {
        CFont::SetCentreOff();
        CFont::SetRightJustifyOff();
        CFont::SetJustifyOn();
    }

    CFont::PrintString(x, y, text.c_str());
}

void Hud::DrawStatusMessages(int fontStyle) {
    const unsigned int now = static_cast<unsigned int>(CTimer::m_snTimeInMilliseconds);

    if (showSelection_ && now < selectionExpiresAt_) {
        if (showSelectionIcon_) {
            const float left = SCREEN_COORD_LEFT(25.0F);
            const float categoryY = SCREEN_COORD_BOTTOM(365.0F);
            const float destinationY = SCREEN_COORD_BOTTOM(330.0F);
            constexpr float iconSize = 24.0F;
            CSprite2d *icon = ResolveDestinationSprite(selectionIcon_);

            DrawText(selectionCategory_, left, categoryY, selectionColor_, fontStyle, false);

            if (icon && icon->m_pTexture) {
                icon->Draw(CRect(left, destinationY,
                                 left + SCREEN_MULTIPLIER(iconSize),
                                 destinationY + SCREEN_MULTIPLIER(iconSize)),
                           CRGBA(255, 255, 255, 255));
            }

            DrawText(selection_, SCREEN_COORD_LEFT(57.0F), destinationY, selectionColor_, fontStyle,
                     false);
        } else {
            DrawText(selection_, SCREEN_COORD_LEFT(25.0F), SCREEN_COORD_BOTTOM(330.0F),
                     selectionColor_, fontStyle, false);
        }
    }

    if (showFare_ && now < fareExpiresAt_) {
        DrawText(fare_, SCREEN_WIDTH / 2.0F, SCREEN_HEIGHT * 0.8F, CRGBA(232, 156, 38, 255),
                 fontStyle, true);
    }
}

void Hud::DrawDestinationBrowser(int fontStyle) {
    if (!showBrowser_ || browserDestinations_.empty()) {
        return;
    }

    constexpr std::size_t maxVisible = 7;
    std::size_t first =
        browserSelectedIndex_ > maxVisible / 2 ? browserSelectedIndex_ - maxVisible / 2 : 0;

    if (first + maxVisible > browserDestinations_.size()) {
        first =
            browserDestinations_.size() > maxVisible ? browserDestinations_.size() - maxVisible : 0;
    }

    const std::size_t last = std::min(first + maxVisible, browserDestinations_.size());
    const float panelLeft = SCREEN_COORD_LEFT(40.0F);
    const float panelTop = SCREEN_COORD_TOP(280.0F);
    const float panelRight = SCREEN_COORD_LEFT(550.0F);
    const float rowHeight = SCREEN_MULTIPLIER(27.0F);
    constexpr float listTopMargin = 30.0F;
    constexpr float listBottomMargin = 18.0F;
    const float panelBottom = panelTop + SCREEN_MULTIPLIER(listTopMargin) +
                              rowHeight * static_cast<float>(last - first) +
                              SCREEN_MULTIPLIER(listBottomMargin + 56.0F);

    CSprite2d::DrawRect(CRect(panelLeft, panelTop, panelRight, panelBottom), CRGBA(0, 0, 0, 205));

    const float iconLeft = SCREEN_COORD_LEFT(52.0F);
    const float textLeft = SCREEN_COORD_LEFT(80.0F);
    CFont::SetBackgroundOff();
    CFont::SetScale(SCREEN_MULTIPLIER(1.33F), SCREEN_MULTIPLIER(2.67F));
    CFont::SetPropOn();
    CFont::SetDropShadowPosition(2);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetFontStyle(2); // VC Pricedown, verified against TestMenu.
    CFont::SetColor(CRGBA(219, 129, 193, 255));
    CFont::SetCentreOff();
    CFont::SetRightJustifyOff();
    CFont::SetJustifyOn();
    CFont::PrintString(textLeft, SCREEN_COORD_TOP(250.0F), browserCategory_.c_str());

    // Keep the list visually separated from both the floating title and controls.
    float y = panelTop + SCREEN_MULTIPLIER(listTopMargin);

    for (std::size_t i = first; i < last; ++i) {
        const bool selected = i == browserSelectedIndex_;

        if (selected) {
            CSprite2d::DrawRect(CRect(panelLeft + SCREEN_MULTIPLIER(8.0F),
                                      y - SCREEN_MULTIPLIER(2.0F),
                                      panelRight - SCREEN_MULTIPLIER(7.0F),
                                      y + rowHeight - SCREEN_MULTIPLIER(3.0F)),
                                CRGBA(143, 72, 123, 170));
        }

        constexpr float iconSize = 20.0F;
        const float iconTop = y + (rowHeight - SCREEN_MULTIPLIER(iconSize)) * 0.5F;
        CSprite2d *icon = ResolveDestinationSprite(browserDestinations_[i].icon);

        if (icon && icon->m_pTexture) {
            icon->Draw(CRect(iconLeft, iconTop, iconLeft + SCREEN_MULTIPLIER(iconSize),
                             iconTop + SCREEN_MULTIPLIER(iconSize)),
                       CRGBA(255, 255, 255, 255));
        }

        DrawText(browserDestinations_[i].name, textLeft, y, CRGBA(245, 245, 245, 255), fontStyle,
                 false);
        y += rowHeight;
    }

    y += SCREEN_MULTIPLIER(listBottomMargin);

    CFont::SetBackgroundOff();
    CFont::SetScale(SCREEN_MULTIPLIER(0.58F), SCREEN_MULTIPLIER(1.05F));
    CFont::SetPropOn();
    CFont::SetDropShadowPosition(1);
    CFont::SetDropColor(CRGBA(0, 0, 0, 255));
    CFont::SetFontStyle(1);
    CFont::SetColor(CRGBA(190, 190, 190, 255));
    CFont::SetCentreOff();
    CFont::SetRightJustifyOff();
    CFont::SetJustifyOn();
    CFont::PrintString(textLeft, y, "Left/Right group   Up/Down destination");

    y += SCREEN_MULTIPLIER(21.0F);
    CFont::PrintString(textLeft, y, "Shift select   Backspace close");
}

CSprite2d *Hud::ResolveDestinationSprite(DestinationIcon icon) {
    using I = DestinationIcon;

    switch (icon) {
    case I::Lawyer:
        return &CRadar::LawyerSprite;
    case I::Tshirt:
        return &CRadar::TshirtSprite;
    case I::Boatyard:
        return &CRadar::BoatyardSprite;
    case I::Strip:
        return &CRadar::StripSprite;
    case I::Gun:
        return &CRadar::GunSprite;
    case I::Spray:
        return &CRadar::SpraySprite;
    case I::Save:
        return &CRadar::SaveSprite;
    case I::Club:
        return &CRadar::ClubSprite;
    case I::Avery:
        return &CRadar::AverySprite;
    case I::DiazMansion:
        return ResolveDiazMansionSprite();
    case I::Mall:
        return mallSprite_.m_pTexture ? &mallSprite_ : &CRadar::ArrowSprite;
    case I::FilmStudio:
        return &CRadar::FilmStudioSprite;
    case I::Vrock:
        return &CRadar::RVRockSprite;
    case I::Bikers:
        return &CRadar::BikersSprite;
    case I::Printworks:
        return &CRadar::PrintworksSprite;
    case I::Icecream:
        return &CRadar::IcecreamSprite;
    case I::Kcabs:
        return &CRadar::KCabsSprite;
    case I::Haitians:
        return &CRadar::HaitiansSprite;
    case I::Phil:
        return &CRadar::PhilSprite;
    case I::Sunyard:
        return &CRadar::SunYardSprite;
    case I::Cubans:
        return &CRadar::CubansSprite;
    case I::MapHere:
    default:
        return &CRadar::ArrowSprite;
    }
}

CSprite2d *Hud::ResolveDiazMansionSprite() {
    constexpr std::size_t radarTraceCount = 75;

    if (CRadar::ms_RadarTrace) {
        for (std::size_t i = 0; i < radarTraceCount; ++i) {
            const tRadarTrace &trace = CRadar::ms_RadarTrace[i];
            if (!trace.m_bInUse) {
                continue;
            }

            if (trace.m_nRadarSprite == RADAR_SPRITE_TOMMY) {
                return &CRadar::TommySprite;
            }
        }
    }

    // Both an active Diaz marker and the no-marker fallback use Diaz's native sprite.
    return &CRadar::DiazSprite;
}

void Hud::Help(const std::string &text, bool quick) {
    const int length = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);

    if (length <= 0) {
        return;
    }

    std::vector<wchar_t> wide(static_cast<std::size_t>(length));
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), length);
    CHud::SetHelpMessage(wide.data(), quick);
}
