#pragma once

#include "core/TaxiTypes.h"

#include <CRGBA.h>
#include <CSprite2d.h>
#include <RenderWare.h>

#include <filesystem>
#include <string>
#include <vector>

class Hud final {
  public:
    void ShowSelection(std::string text, CRGBA color, unsigned int durationMs);
    void ShowFare(int fare);
    void ShowHelp(std::string text);
    void HideHelp() noexcept;
    void UpdateHelp();
    void InitializeTextures(const std::filesystem::path &moduleDirectory);
    void ShutdownTextures() noexcept;
    void HideSelection() noexcept {
        showSelection_ = false;
    }

    void ShowBrowser(std::string category, std::vector<DestinationBrowserRow> destinations,
                     std::size_t selectedIndex);
    void HideBrowser() noexcept {
        showBrowser_ = false;
    }

    void Reset() noexcept;
    void Draw(int fontStyle);
    static void Help(const std::string &text, bool quick = true);

  private:
    std::string selection_;
    std::string fare_;
    CRGBA selectionColor_{232, 156, 38, 255};
    bool showSelection_{};
    unsigned int selectionExpiresAt_{};
    bool showFare_{};
    unsigned int fareExpiresAt_{};
    std::string help_;
    unsigned int helpShowAt_{};
    bool helpPending_{};
    std::string browserCategory_;
    std::vector<DestinationBrowserRow> browserDestinations_;
    std::size_t browserSelectedIndex_{};
    bool showBrowser_{};
    RwTexDictionary *mallDictionary_{};
    CSprite2d mallSprite_{};

    static void DrawText(const std::string &text, float x, float y, CRGBA color, int fontStyle,
                         bool centred);
    CSprite2d *ResolveDestinationSprite(DestinationIcon icon);
    static CSprite2d *ResolveDiazMansionSprite();
    void DrawStatusMessages(int fontStyle) const;
    void DrawDestinationBrowser(int fontStyle);
};
