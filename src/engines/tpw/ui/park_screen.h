// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "common/system.h"
#include "common/types.h"
#include "engines/tpw/formats/bf4.h"
#include "engines/tpw/formats/str.h"
#include "engines/tpw/park/attractions.h"
#include "engines/tpw/park/park.h"
#include "engines/tpw/park/path_tiles.h"
#include "engines/tpw/ui/advisor.h"
#include "engines/tpw/ui/buy_window.h"
#include "engines/tpw/ui/hire_window.h"
#include "engines/tpw/ui/info_windows.h"
#include "engines/tpw/ui/park_windows.h"
#include "engines/tpw/ui/park_hud.h"
#include "engines/tpw/ui/ride_run.h"
#include "engines/tpw/ui/sign_painter.h"
#include "engines/tpw/ui/sprites.h"
#include "engines/tpw/ui/world_view.h"
#include "graphics/soft3d/rasterizer.h"

#include <array>
#include <functional>
#include <deque>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace reluva::tpw {

/**
 * The park as the player sees it: its world drawn through a camera that
 * pans, turns and zooms over it, its fixed items marked, its visitors by
 * their sprites, and the HUD over it, with the money and date. See
 * docs/engines/tpw/games/simcoaster/systems/park.md.
 */
class ParkScreen {
public:
    /** Where the camera looks from: at `target`, from `distance` units, turned and tilted (radians). */
    struct Camera {
        gfx::Vec3 target{};
        float yaw = 0;
        float pitch = 0;
        float distance = 0;
    };

    /** Reads the park's world, the HUD's models and fonts, and the months' names. Fails without the world. */
    Error load(const SearchSet &files, const Park &park);

    /** The camera a new park starts with: over its entrance, looking in. */
    Camera startCamera(const Park &park) const;
    /** A ride's view (its menu's Ride): close in over it, turned as `from`. */
    Camera rideCamera(const Park &park, size_t object, const Camera &from) const;

    /** Draws the park through `camera` over the whole of an RGBA32 surface. */
    void draw(Surface &screen, soft3d::Rasterizer &rasterizer, const Park &park, const Camera &camera) const;
    /**
     * Draws the park alone, its world, paths, objects, visitors and
     * weather, without the HUD, through `eye`, as the fly-in sees it;
     * `yaw` the way it faces, as Camera::yaw.
     */
    void drawScene(Surface &screen, soft3d::Rasterizer &rasterizer, const Park &park, const soft3d::OrbitCamera &eye,
                   float yaw) const;

    /**
     * The park's cell under a point of the screen, on the ground (y = 0),
     * seen through `camera` at 640 × 480.
     * @return False if the point looks above the horizon.
     */
    bool cellAt(const Camera &camera, int x, int y, Park::Cell &out) const;

    /** Where the middle of a cell is on the screen, seen through `camera` at 640 × 480; false if behind it. */
    bool cellOnScreen(const Camera &camera, const Park::Cell &cell, int &x, int &y) const;

    /**
     * Cells drawn as path though not yet built, the length the path tool
     * is laying; those it can't build on drawn marked red.
     */
    void setPathPreview(std::vector<Park::Cell> cells) { _preview = std::move(cells); }
    /** A queue drawn as it would be with the length being laid, in place of the park's of its entrance. */
    void setQueuePreview(std::optional<Park::Queue> queue) { _queuePreview = std::move(queue); }

    /** The world's attractions, for the Buy window and placing them. */
    const Attractions &attractions() const { return _attractions; }
    /** The Buy window, which takes the mouse before the HUD while it's open. */
    BuyWindow &buyWindow() { return _buy; }
    /**
     * The footprint of an attraction being placed, drawn on the ground,
     * blue where it fits, red where it doesn't; and its price under the
     * money (0 for none).
     */
    void setPlacement(std::optional<Park::Footprint> footprint, bool fits, int price) {
        _placement = std::move(footprint);
        _placementFits = fits;
        _placementPrice = price;
    }

    /**
     * Runs the placed objects' scripts on to the park's time: rides,
     * shops and features moving as their scripts have them, the rides
     * taking the park's visitors waiting at their entrances and letting
     * them off.
     */
    void advance(Park &park);
    /** Runs the objects' scripts on by `ms`, the park's clock standing still, as in the fly-in. */
    void advanceScripts(Park &park, uint32_t ms);

    /** The Hire Staff window, which takes the mouse before the HUD while it's open; the staff's sprites. */
    HireWindow &hireWindow() { return _hire; }

    /** The park's smaller windows and right-click menu, and what draws them. */
    ///@{
    const WindowKit &kit() const { return _kit; }
    PopupMenu &popup() { return _popup; }
    AttractionWindow &attractionWindow() { return _attractionWindow; }
    GateWindow &gateWindow() { return _gateWindow; }
    StaffWindow &staffWindow() { return _staffWindow; }
    AbilityWindow &abilityWindow() { return _abilityWindow; }
    EmployeeWindow &employeeWindow() { return _employeeWindow; }
    /**
     * Asks what a member of staff's to learn, if they may go for
     * training; else says why not over the park.
     */
    void chooseAbility(Park &park, int staff);
    StaffListWindow &staffList() { return _staffList; }
    TrainingWindow &trainingWindow() { return _trainingWindow; }
    StaffFinanceWindow &staffFinance() { return _staffFinance; }
    WageDemandWindow &wageDemand() { return _wageDemand; }
    ResearchLabWindow &researchLab() { return _researchLab; }
    ProjectWindow &projectWindow() { return _projectWindow; }
    AssignWindow &assignWindow() { return _assignWindow; }
    FinanceSummaryWindow &financeSummary() { return _financeSummary; }
    RenameWindow &renameWindow() { return _renameWindow; }
    FinanceStatsWindow &financeStats() { return _financeStats; }
    LoansWindow &loansWindow() { return _loansWindow; }
    PortfolioWindow &portfolio() { return _portfolio; }
    AttractionFinanceWindow &attractionFinance() { return _attractionFinance; }
    MessageWindow &messageWindow() { return _messageWindow; }
    VisitorStatsWindow &visitorStats() { return _visitorStats; }
    VisitorWindow &visitorWindow() { return _visitorWindow; }
    VisitorListWindow &visitorList() { return _visitorList; }
    AttractionListWindow &attractionList() { return _attractionList; }
    ParkStatsWindow &parkStats() { return _parkStats; }
    AwardsWindow &awards() { return _awards; }
    MessageCenterWindow &messageCenter() { return _messageCenter; }
    GameMenu &gameMenu() { return _gameMenu; }
    ParkMapWindow &parkMap() { return _parkMap; }
    OverviewMapWindow &overviewMap() { return _overviewMap; }
    SaveLoadWindow &saveLoad() { return _saveLoad; }
    OptionsWindow &optionsWindow() { return _optionsWindow; }
    /**
     * The options followed: the advisor on or off, the volumes on the
     * mixer (none for nullptr), the game's speed by playPark; kept in the
     * save folder as they change (none kept without one).
     */
    void setOptions(const GameOptions &options, std::filesystem::path saveFolder, audio::Mixer *mixer);
    /** Changes them, as the Options window has: followed and kept. */
    void changeOptions(const GameOptions &options);
    const GameOptions &options() const { return _options; }
    /** Where the player's games are saved, and whose; an empty folder saves nothing. */
    void setSaves(std::filesystem::path folder, std::string player) {
        _savesFolder = std::move(folder);
        _saver = std::move(player);
    }
    /** Opens Save Game or Load Game on the player's saves. */
    void openSaveLoad(SaveLoadWindow::Mode mode);
    /** Writes the game as the Save Game window chose: over an old save, or a new one. */
    Error saveChosen(const Park &park);
    /** Deletes the save the window's been told to, if any, and lists the rest. */
    void deleteChosen();
    /** Takes the save the Load Game window chose. */
    void takeLoadChosen() { _loadRequest = _slotFiles[size_t(_saveLoad.chosen())]; }
    /** The save to load in this park's place; empty for none. */
    const std::filesystem::path &loadRequest() const { return _loadRequest; }
    /**
     * Saves the game as Select A New Player does, named for its world and
     * date ("Land of Invention 11.Jan.00"), over a save of that name, and
     * asks for the Personnel File.
     */
    Error saveForNewPlayer(const Park &park);
    /** Writes the game to a file under a name. */
    Error saveAs(const Park &park, const std::string &name, const std::filesystem::path &file) const;
    /** Whether the player chose Select A New Player: the Personnel File comes next. */
    bool newPlayerRequested() const { return _newPlayer; }
    Advisor &advisor() { return _advisor; }
    const Advisor &advisor() const { return _advisor; }
    ActivateRideWindow &activateRide() { return _activateRide; }
    /** The messages received, oldest first; marks them all read. */
    std::vector<MessageCenterWindow::Received> &inbox() { return _inbox; }
    void markRead() {
        for (MessageCenterWindow::Received &m : _inbox)
            m.read = true;
    }
    ///@}
    /** The player whose park it is, for their share portfolio: their name and rank (PROMOTIONLEVELS.str's). */
    void setPlayer(std::string name, int rank) {
        _playerName = std::move(name);
        _playerRank = rank;
    }
    /** Whether a world (as Park numbers themes) is open to the player, and its challenges, as their file has it. */
    void setWorld(int theme, bool open, int challenges) {
        if (theme >= 0 && theme < int(_worldOpen.size())) {
            _worldOpen[size_t(theme)] = open;
            _worldChallenges[size_t(theme)] = challenges;
        }
    }
    /**
     * The worlds for the Overview Map, in the globe's order: open as the
     * player's file and the missions done here have it; the park's own
     * world always.
     */
    /** Whether a point of the screen is on the billboard outside the gates (the world's model's `signpost`). */
    bool onBillboard(const Camera &camera, int x, int y) const;
    std::vector<OverviewMapWindow::World> worlds(const Park &park) const;
    /**
     * What a saved game keeps of the screen: the Message Center's
     * messages, and the advisor's lines said. See
     * docs/engines/tpw/games/simcoaster/formats/reluva-saves.md.
     */
    std::vector<uint8_t> saveState() const;
    /** Puts it back; fails on a state cut short or of another version. */
    Error restoreState(const std::vector<uint8_t> &data);
    /** The world the Overview Map chose to go to, as Park numbers themes; −1 for none. */
    int travel() const { return _travel; }
    void setTravel(int theme) { _travel = theme; }
    const std::string &playerName() const { return _playerName; }
    int playerRank() const { return _playerRank; }
    /** Takes the park's new messages, and shows the next waiting once none is showing. */
    void takeMessages(Park &park);
    /** Whether a message is showing or waiting. */
    bool messagesWaiting() const { return _messageWindow.isOpen() || !_messages.empty(); }
    /** Shows a message, UITEXT's, over the park for a few seconds of the park's time. */
    void notify(const Park &park, uint32_t text);
    /** Forgets what it ran for an object `Park::remove` just took away, and its queue, −1 for none. */
    void removed(size_t object, int queue);
    /** Whether any of the windows is open, which take the mouse before the HUD. */
    bool windowOpen() const;
    /** The object one of whose cells a cell is; −1 for none. */
    int objectAt(const Park &park, const Park::Cell &cell) const;
    /** The member of staff drawn under a point of the screen, seen through `camera`; 0 for none. */
    int staffAt(const Park &park, const Camera &camera, int x, int y) const;
    /** Likewise a visitor in the park, walking or queueing; 0 for none. */
    int visitorAt(const Park &park, const Camera &camera, int x, int y) const;
    /**
     * The tooltip for what's under a point of the park, as the pointer
     * finds it (UIHELPTEXT's): a member of staff, a visitor, an attraction
     * or obstacle, the gates; empty for nothing.
     */
    std::string tipAt(const Park &park, const Camera &camera, int x, int y) const;
    /**
     * The tooltip to show by the pointer, from now (in the system's
     * milliseconds): it shows once the same text has stood a while.
     */
    void setTip(std::string text, int x, int y, uint64_t now);
    /** The camcorder's view (the tool's): at a visitor's eye height over a cell, turned as `from`. */
    Camera groundCamera(const Park::Cell &cell, const Camera &from) const;
    /** An object's name as the game shows it, and its model; nullptr if it hasn't one. */
    std::string nameOf(const Park &park, size_t object) const;
    const LoadedModel *modelOf(const Park &park, size_t object) const;

    /** The HUD's buttons, which take the mouse before the park does. */
    ParkHud &hud() { return _furniture; }

    /** The date as the HUD shows it: "14.Jan.00". */
    std::string dateText(uint32_t day) const;

private:
    /** A visitor seen on a ride, and where their feet are, in the world's units. */
    struct Rider {
        int id = 0;
        gfx::Vec3 at{}; // a seated rider's head; a bouncer's feet
        bool seated = false;
        float headingX = 0; // the way a seated rider faces, along the ground
        float headingZ = 1;
    };
    std::vector<Rider> riders(const Park &park) const;
    void drawVisitors(Surface &screen, const soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye,
                      const Park &park, const Camera &camera) const;
    void drawWeather(Surface &screen, const Park &park) const;
    void drawPaths(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const Park &park) const;
    void drawObjects(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const Park &park) const;
    void drawPlacement(Surface &screen, const soft3d::OrbitCamera &eye) const;
    void loadObjects(const SearchSet &files, const Park &park);
    const LoadedModel *objectModel(int id) const;
    /**
     * A track ride's piece's model, or its cars' (`model` the vehicle's
     * number), read from beside its own the first time; null where there's none.
     */
    const LoadedModel *trackModel(int id, Park::TrackModel model) const;
    const LoadedModel *vehicleModel(int id, size_t vehicle) const;
    void drawTracks(soft3d::Rasterizer &rasterizer, const soft3d::OrbitCamera &eye, const Park &park,
                    const soft3d::Vec &light) const;
    // Its name on its sign, if it has one.
    void letterSign(int id, const std::string &path, LoadedModel &model) const;

    LoadedWorld _world;
    CharacterSet _characters;
    Font _cashFont;
    Font _dateFont;
    StringTable _months;
    bool _hud = false; // the fonts and months read
    ParkHud _furniture;
    std::string _tip;
    int _tipX = 0;
    int _tipY = 0;
    uint64_t _tipSince = 0; // when the tip's text came
    uint64_t _tipNow = 0;
    std::array<image::Image, kPathTextures.size()> _pathTextures;
    bool _pathsRead = false;
    std::vector<Park::Cell> _preview;
    mutable std::map<int, LoadedModel> _objectModels; // by the objects' numbers, those read
    mutable std::set<int> _missingModels;             // those that couldn't be, not tried again
    // Track rides' pieces' and cars' models, by their rides' numbers and
    // then Park::TrackModel, or the car's number in the ride's settings;
    // those that couldn't be read, by such keys, are kept as null.
    mutable std::map<std::pair<int, int>, std::unique_ptr<LoadedModel>> _trackModels;
    mutable std::map<std::pair<int, int>, std::unique_ptr<LoadedModel>> _vehicleModels;
    mutable std::map<int, std::string> _objectPaths; // their models' names
    mutable SignPainter _signs;
    std::vector<std::unique_ptr<RideRun>> _runs;      // by the park's objects: their scripts, those that have one
    std::vector<int> _runIds;                         // the objects they're of
    std::vector<std::unique_ptr<RideRun>> _fixedRuns; // by Park::fixedObjects()
    bool _lettingOff = true;                          // the park's riders, as it's first run
    uint64_t _ranTo = 0;                              // the park's time they're run to
    // Attractions without a script, by their queues: whom they have, and till when (the park's time).
    std::map<int, std::vector<std::pair<int, uint64_t>>> _unscripted;
    size_t _prebuilt = 0; // the objects there as the park opened
    const SearchSet *_files = nullptr;
    Attractions _attractions;
    BuyWindow _buy;
    bool _buyRead = false;
    HireWindow _hire;
    bool _hireRead = false;
    WindowKit _kit;
    bool _kitRead = false;
    PopupMenu _popup;
    AttractionWindow _attractionWindow;
    GateWindow _gateWindow;
    StaffWindow _staffWindow;
    AbilityWindow _abilityWindow;
    EmployeeWindow _employeeWindow;
    StaffListWindow _staffList;
    TrainingWindow _trainingWindow;
    StaffFinanceWindow _staffFinance;
    WageDemandWindow _wageDemand;
    ResearchLabWindow _researchLab;
    ProjectWindow _projectWindow;
    AssignWindow _assignWindow;
    FinanceSummaryWindow _financeSummary;
    RenameWindow _renameWindow;
    FinanceStatsWindow _financeStats;
    LoansWindow _loansWindow;
    PortfolioWindow _portfolio;
    AttractionFinanceWindow _attractionFinance;
    MessageWindow _messageWindow;
    VisitorStatsWindow _visitorStats;
    VisitorWindow _visitorWindow;
    VisitorListWindow _visitorList;
    AttractionListWindow _attractionList;
    ParkStatsWindow _parkStats;
    AwardsWindow _awards;
    MessageCenterWindow _messageCenter;
    GameMenu _gameMenu;
    ParkMapWindow _parkMap;
    OverviewMapWindow _overviewMap;
    SaveLoadWindow _saveLoad;
    OptionsWindow _optionsWindow;
    GameOptions _options;
    std::filesystem::path _optionsFolder;
    audio::Mixer *_mixer = nullptr;
    std::filesystem::path _savesFolder;
    std::string _saver;
    std::vector<std::filesystem::path> _slotFiles; // the Save and Load windows' rows' files
    std::filesystem::path _loadRequest;
    bool _newPlayer = false;
    Advisor _advisor;
    std::array<bool, 3> _worldOpen{};
    std::array<int, 3> _worldChallenges{10, 10, 10}; // ChallengesInThisLevel's ten each
    int _travel = -1;
    ActivateRideWindow _activateRide;
    std::vector<MessageCenterWindow::Received> _inbox;
    std::deque<Park::Message> _messages; // waiting to be shown
    std::string _playerName;
    int _playerRank = 1;
    uint32_t _notice = 0;      // UITEXT's; 0 for none
    uint64_t _noticeUntil = 0; // the park's time
    std::optional<Park::Footprint> _placement;
    bool _placementFits = false;
    int _placementPrice = 0;
    std::optional<Park::Queue> _queuePreview;
    std::map<int, LoadedModel> _queueModels; // by Park::QueueModel, those read
    std::map<int, LoadedModel> _fenceModels; // by Park::FenceModel and look, as fenceKey makes them

    bool _furnitureRead = false;
    std::array<std::optional<LoadedSprite>, Park::kPeople> _personLooks; // by Park::Visitor::person, those read
    std::array<std::optional<LoadedSprite>, Park::kPeople> _personHeads; // their heads alone, for riders
    std::array<float, Park::kPeople> _umbrellaHeights{};
    std::optional<LoadedSprite> _litterSprite;
    std::optional<LoadedSprite> _thoughtSprite;
    std::optional<LoadedSprite> _moreThoughtSprite;
    std::optional<LoadedSprite> _umbrellaSprite;
    std::optional<LoadedSprite> _balloonSprite;
    std::optional<LoadedSprite> _particleSprite;
    std::optional<LoadedSprite> _costume; // the world's, whole and its head
    std::optional<LoadedSprite> _costumeHead;
    image::Image _snow;      // four flakes, a quarter each
    image::Image _lightning; // a bolt, top to bottom
};

/** Shows the mission's briefing over a background it's given a way to draw: on its Challenges tab, if asked. */
using ObjectivesShower = std::function<bool(const std::function<void(Surface &)> &drawPark, bool challenges)>;

/**
 * Plays the park until the player leaves it (Esc) or the window is
 * closed: its clock runs, the arrow keys move the camera over it, Q and E
 * turn it, Page Up and Page Down (or the wheel) zoom, Home goes back to
 * the start; the HUD's buttons and menus work, and the path tool lays
 * path where it's clicked.
 * @param showObjectives Shows the mission's briefing, for the Park menu's
 *     Objectives, over the park as the function it's given draws it;
 *     returns false if the window was closed meanwhile.
 * @return False if the window was closed (or the launcher asked for).
 */
bool playPark(System &sys, ParkScreen &screen, Park &park, const ObjectivesShower &showObjectives = {});

} // namespace reluva::tpw
