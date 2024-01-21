/* Standard headers */
#include <iostream>
#include <thread>
#include <future>
#include <algorithm>
#include <format>

/* Third Party headers */
#define RAYGUI_IMPLEMENTATION
#include <raylib.h>
#include "raygui.h" // tweaked for compatibility
extern "C" {
#include "cyber/style_cyber.h"
};
#include <curl/curl.h>
#include <json/json.h>

#define BASE_API_URL "http://127.0.0.1"
#define BASE_API_PORT 5540
#define DEFAULT_WIN_TITLE "Time Tracker: Log work time!"

/**
 * TODO: Add functionality to edit the time on a track
 * TODO: Prompt before track delete
 * TODO: Add functionality to HEARTBEAT the server for status
 * TODO: JSON Web Tokens (JWT) + Periodic automatic re-auth
 * TODO: Add functionality to change the server address
 * TODO: Add functionality to save/load for offline usage
 * TODO: Harden the security a tad
 * TODO: Refactor for niceness
 */

// TODO: (NOT RELEVANT) WebRadio project \
    GUI interface with raylib \
    Also hosts the website (drogon-core?) \
    Direct music connection with this app?

/* Helpful Methods */
inline Color RGBToColor(unsigned char r, unsigned char g, unsigned char b) {
    return Color(r, g, b, 255U);
}

/* API implementation */

typedef std::future<std::pair<bool, std::string>> APIResult;

struct AuthToken {
    std::string token;
    std::string username;
    uint64_t userid;
    std::chrono::time_point<std::chrono::system_clock> expiration;
};

/**
 * libcurl curl_easy_* WriteFunction for std::string
 * @param data The recv'd bytes
 * @param chunkSize Size per chunk
 * @param numChunks Number of chunks recv'd
 * @param str Pointer to string buffer
 * @return Total size of bytes recv'd
 */
static size_t curl_easy_writefn_str(void *data, size_t chunkSize, size_t numChunks, std::string *str);

/**
 * Send a POST request to a URL
 * @param apiUrl URL to send a request to
 * @param postData The POST data (format "key=value&key1=value1...")
 * @return std::pair<bool, std::string>{success, message}
 */
APIResult MakeAPICall(std::string&& apiUrl, std::string&& postData);

/* Custom GUI implementation */

class CountButton {
public:
    CountButton() = default;

    /**
     * CountButton render
     * @param x x position
     * @param y y position
     * @param r Circle radius (inclusive)
     * @return Boolean for whether button was clicked
     */
    bool draw(int x, int y, int r);

    /**
     * Toggle the button state
     */
    void toggleCounting();

    /**
     * Check if the button state is counting
     * @return Boolean for whether button is active (counting)
     */
    bool isCounting() const;

private:
    const char* m_startText = "START COUNTING";
    const char* m_stopText = "STOP COUNTING";
    Color m_buttonColorInitial = RGBToColor(103U, 252U, 28U);
    Color m_buttonColorCounting = RGBToColor(252U, 110U, 28U);
    bool m_isCounting = false;

    /**
     * Check if the mouse is hovering over the button
     * @param x x position
     * @param y y position
     * @param r circle radius (inclusive)
     * @return Boolean for whether mouse is hovering button
     */
    inline bool isHover(int x, int y, int r) {
        return CheckCollisionPointCircle(GetMousePosition(), Vector2 {static_cast<float>(x), static_cast<float>(y)}, static_cast<float>(r));
    }
};

/* Conversions */

/**
 * Convert seconds to HHMMSS format
 * @param seconds Seconds
 * @return String in HHMMSS format
 */
std::string SecondsToHMS(uint64_t seconds);

/* Application Details */
struct ApplicationDetails {
    APIResult apicall;
    AuthToken auth{};
    std::string trackName{};
    uint64_t sessionSeconds{}, savedSeconds{};
    std::chrono::time_point<std::chrono::system_clock> start{};
    bool promptedClose{}, shouldClose{}, promptedLogout{}, tracksCached{};
    std::tuple<bool, std::string, std::chrono::time_point<std::chrono::system_clock>> lastMessage{};
    std::vector<std::string> trackNames{};
};

/* Other pages */

/**
 * Draw the login screen
 * @param apicall The APIResult to handle login / register
 */
void DrawLogin(APIResult * apicall);

/**
 * Draw the project picker screen
 */
void DrawProjectPicker(ApplicationDetails& details);

/* Entry point */

int main(int argc, char** argv) {
    InitWindow(600, 800, DEFAULT_WIN_TITLE);
    SetTargetFPS(30);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    GuiLoadStyleCyber();

    ApplicationDetails appDetails{};
    appDetails.start = std::chrono::system_clock::now();

    APIResult& apicall = appDetails.apicall;
    AuthToken& auth = appDetails.auth;
    std::string& trackName = appDetails.trackName;
    uint64_t& sessionSeconds = appDetails.sessionSeconds, &savedSeconds = appDetails.savedSeconds;
    std::chrono::time_point<std::chrono::system_clock>& start = appDetails.start;
    bool& promptedClose = appDetails.promptedClose, &shouldClose = appDetails.shouldClose, &promptedLogout = appDetails.promptedLogout, &tracksCached = appDetails.tracksCached;
    std::tuple<bool, std::string, std::chrono::time_point<std::chrono::system_clock>>& lastMessage = appDetails.lastMessage;
    std::vector<std::string>& trackNames = appDetails.trackNames;

    auto apicall_isReady = [&apicall]() {
        return apicall.valid() && apicall.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    };

    auto time_expired = [](std::chrono::time_point<std::chrono::system_clock>& tp, uint64_t duration) {
        if(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - tp).count() > duration) {
            return true;
        } else return false;
    };

    CountButton CountButton;
    Color backgroundColor = RGBToColor(41U, 44U, 51U);



    while(!shouldClose) {
        if(WindowShouldClose()) promptedClose = true;

        if(apicall_isReady()) {
            // Handle API response data
            auto data = apicall.get();
            std::cout << "API Call: " << (data.first ? "Success" : "Error") << std::endl;
            std::cout << "API Result: " << data.second << std::endl;
            lastMessage = {data.first, data.second, std::chrono::system_clock::now()};

            try {
                Json::Reader reader;
                Json::Value root;
                if (!reader.parse(data.second, root)) {
                    // A parsing error has occurred
                    data.first = false;
                    data.second = reader.getFormattedErrorMessages();
                } else {
                    // Parse was successful
                    if (root.isMember("error")) {
                        // An API error has occurred
                        std::get<0>(lastMessage) = false;
                        std::get<1>(lastMessage) = root["error"].asString();
                    } else if (root.isMember("behavior")) {
                        // Expected API behavior
                        std::get<0>(lastMessage) = true;
                        std::string behavior = root["behavior"].asString();
                        std::get<1>(lastMessage) = root.isMember("message") ? root["message"].asString()
                                                                            : std::string();
                        if (behavior == "VERSION") {
                            // VERSION DETAILS
                            if (root.isMember("name")) printf("Application Name: %s\n", root["name"].asCString());
                            if (root.isMember("description"))
                                printf("Application Name: %s\n", root["description"].asCString());
                            if (root.isMember("version")) printf("Application Name: %s\n", root["version"].asCString());
                        } else if (behavior == "AUTHENTICATION") {
                            // LOG IN
                            if (!root.isMember("username") || !root.isMember("uid")) {
                                // Malformed
                                std::get<0>(lastMessage) = false;
                                std::get<1>(lastMessage) = "Bad auth.";
                            } else {
                                // Successful
                                std::string newWinTitle = "(" + root["username"].asString() + ") Time Tracker";
                                SetWindowTitle(newWinTitle.c_str());
                                auth.username = root["name"].asString();
                                auth.userid = root["uid"].asUInt64();
                                auth.token = "filled"; // NOTICE: TEMPORARY
                                continue;
                            }
                        } else if (behavior == "ACCOUNT") {
                            // ACCOUNT DETAILS
                            Json::StreamWriterBuilder writeBuilder;
                            std::string details = Json::writeString(writeBuilder, root);

                            printf("Account details: %s\n", details.c_str());

                            if (root.isMember("tracks") && root["tracks"].isArray()) {
                                for (Json::Value::ArrayIndex i = 0; i != root["tracks"].size(); i++) {
                                    if (root["tracks"][i].isMember("track"))
                                        trackNames.push_back(root["tracks"][i]["track"].asString());
                                }
                            }
                        } else if (behavior == "SAVEACK") {
                            // Saved successfully!
                            savedSeconds += sessionSeconds;
                            sessionSeconds = 0U;
                            continue;
                        } else if (behavior == "TRACKINFO") {
                            // Track update
                            if (root.isMember("seconds")) {
                                savedSeconds = root["seconds"].asUInt64();
                                std::get<1>(lastMessage) = "Synced successfully!";
                            }
                        }
                    } else if (root.isMember("message")) {
                        std::get<0>(lastMessage) = true;
                        std::get<1>(lastMessage) = root["message"].asString();
                    } else {
                        std::get<0>(lastMessage) = false;
                        std::get<1>(lastMessage) = "Unknown request. See stderr for details.";
                        fprintf(stderr, "Unknown response: %s\n", root.asCString());
                    }
                }
            } catch(const std::exception& e) {
                fprintf(stderr, "JsonCpp error: %s\n", e.what());
            }
        }

        BeginDrawing();
        ClearBackground(backgroundColor);

        // Draw the login screen
        if(auth.token.empty()) {
            if(promptedClose) shouldClose = true;
            DrawLogin(&apicall);
            const int fontSize = 14;
            // Draw the lastMessage
            if(!std::get<1>(lastMessage).empty())
                if(!time_expired(std::get<2>(lastMessage), 5ULL)) {
                    DrawText(std::get<1>(lastMessage).c_str(), 300 - (MeasureText(std::get<1>(lastMessage).c_str(), fontSize) / 2), 450, fontSize, std::get<0>(lastMessage) ? WHITE : RED);
                } else lastMessage = {};
            std::string serverMessage = "Server: " + std::string(BASE_API_URL);
            DrawText(serverMessage.c_str(), 300 - (MeasureText(serverMessage.c_str(), fontSize) / 2), 455 + fontSize, fontSize, WHITE);
            EndDrawing();
            continue;
        }

        // Draw the track selection page
        if(trackName.empty()) {
            if(promptedClose) shouldClose = true;

            if(!tracksCached) {
                trackNames.clear();
                apicall = MakeAPICall("/account", "uid=" + std::to_string(auth.userid));
                tracksCached = true;
            }
            DrawProjectPicker(appDetails);
            // Draw the lastMessage
            const int fontSize = 14;
            if(!std::get<1>(lastMessage).empty())
                if(!time_expired(std::get<2>(lastMessage), 5ULL)) {
                    DrawText(std::get<1>(lastMessage).c_str(), 300 - (MeasureText(std::get<1>(lastMessage).c_str(), fontSize) / 2), 450, fontSize, std::get<0>(lastMessage) ? WHITE : RED);
                } else lastMessage = {};
            EndDrawing();
            continue;
        }
        tracksCached = false;

        // Draw the on-exit dialog box
        if(promptedClose) {
            switch(GuiMessageBox(Rectangle {200.f, 250.f, 200.f, 200.f}, "Confirmation Dialogue", "You sure you want to exit?\nYour time may not be saved.", "Yes;No")) {
                case 1:
                    shouldClose = true;
                    break;
                case 0: [[fallthrough]];
                case 2:
                    promptedClose = false;
                    break;
            }

            EndDrawing();
            continue;
        } else if(promptedLogout) {
            // Draw the on-logout dialog box
            bool shouldLogout = false;

            if(sessionSeconds > 0U)
                switch(GuiMessageBox(Rectangle {200.f, 250.f, 200.f, 200.f}, "Confirmation Dialogue", "You sure you want to logout?\nYour time may not be saved.", "Yes;No")) {
                    case 1:
                        shouldLogout = true;
                        break;
                    case 0: [[fallthrough]];
                    case 2:
                        promptedLogout = false;
                        break;
                }
            else shouldLogout = true;

            if(shouldLogout) {
                sessionSeconds = 0U;
                CountButton.isCounting() ? CountButton.toggleCounting() : void();
                auth = {};
                SetWindowTitle(DEFAULT_WIN_TITLE);
                promptedLogout = false;
                trackName = std::string();
                tracksCached = false;
            }

            EndDrawing();
            continue;
        }

        // Determine counting state and draw the button
        bool wasCounting = CountButton.isCounting();
        if(CountButton.draw(300, 400, 220)) {
            CountButton.toggleCounting();
            if(!wasCounting) start = std::chrono::system_clock::now();
        }
        bool isCounting = CountButton.isCounting();

        // Get the # of sessionSeconds passed since counting
        uint32_t uncountedSeconds = CountButton.isCounting() ? std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - start).count() : 0U;

        // Draw the current counting time OR save the duration to sessionSeconds
        if(isCounting) {
            std::string hmsStr = SecondsToHMS(uncountedSeconds);
            DrawText(hmsStr.c_str(), 300 - (MeasureText(hmsStr.c_str(), 36) / 2), 700, 36, WHITE);
        } else if(wasCounting && !isCounting) {
            // Add duration
            std::chrono::time_point end = std::chrono::system_clock::now();
            sessionSeconds += std::chrono::duration_cast<std::chrono::seconds>(end - start).count();
        }

        // Draw the live state of the work log
        DrawText("Total: ", 10, 5, 20, WHITE);
        DrawText(std::to_string(savedSeconds + sessionSeconds + uncountedSeconds).c_str(), 120, 5, 20, WHITE);
        DrawText("Session: ", 10, 35, 20, WHITE);
        DrawText(SecondsToHMS(sessionSeconds + uncountedSeconds).c_str(), 120, 35, 20, WHITE);
        DrawText("Track: ", 10, 65, 20, WHITE);
        DrawText(trackName.c_str(), 120, 65, 20, WHITE);

        // Draw the Sync and Sync & Save and Reset buttons
        // Lock both if awaiting API callback or lock Sync & Save if counting
        if(apicall.valid()) GuiDisable();
        if(GuiButton(Rectangle {10.f, 95.f, 85.f, 25.f}, "Sync")) {
            // Sync with server
            apicall = MakeAPICall("/count", "track=" + trackName + "&uid=" + std::to_string(auth.userid));
        }
        if(isCounting || sessionSeconds == 0U) GuiDisable();
        if(GuiButton(Rectangle {105.f, 95.f, 85.f, 25.f}, "Save")) {
            // Sync and save with server
            apicall = MakeAPICall("/update", "uid=" + std::to_string(auth.userid) + "&track=" + trackName + "&seconds=" + std::to_string(sessionSeconds));
        }
        // Draw the Reset button
        if(GuiButton(Rectangle {295.f, 95.f, 85.f, 25.f}, "Reset")) {
            // Reset session count
            sessionSeconds = 0U;
        }
        GuiEnable();

        // Draw the Logout button
        if(GuiButton(Rectangle {200.f, 95.f, 85.f, 25.f}, "Logout")) {
            // Sign out of session
            promptedLogout = true;
        }

        // Draw the lastMessage
        if(!std::get<1>(lastMessage).empty())
            if(!time_expired(std::get<2>(lastMessage), 5ULL)) {
                const int fontSize = 14;
                DrawText(std::get<1>(lastMessage).c_str(), 395.f, 95.f + (fontSize / 2) + 1.f, fontSize, std::get<0>(lastMessage) ? WHITE : RED);
            } else lastMessage = {};

        EndDrawing();
    }

    curl_global_cleanup();

    CloseWindow();

    return 0;
}

/* Method definitions */

static size_t curl_easy_writefn_str(void *data, size_t chunkSize, size_t numChunks, std::string *str) {
    size_t totalSize = chunkSize * numChunks;
    str->append(static_cast<char*>(data), totalSize);
    return totalSize;
}

APIResult MakeAPICall(std::string&& apiUrl, std::string&& postData) {
    return std::async(std::launch::async, [](std::string&& _apiUrl, std::string&& _postData) -> std::pair<bool, std::string> {
        CURL* curl = curl_easy_init();
        if(curl == nullptr) throw std::runtime_error("Could not initialize CURL.");
        CURLcode res;

        // general configuration
        curl_easy_setopt(curl, CURLOPT_URL, _apiUrl.c_str());
#ifdef BASE_API_PORT
        curl_easy_setopt(curl, CURLOPT_PORT, BASE_API_PORT);
#endif
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        // receive data
        std::string data;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_easy_writefn_str);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

        // send data
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
//        curl_slist* headers = nullptr; // must be set to nullptr FIRST
//        headers = curl_slist_append(headers, "Content-Type: application/json");
//        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, _postData.c_str());

#ifndef NDEBUG
#ifdef BASE_API_PORT
        printf("POST REQUEST: %s:%d%s\n", BASE_API_URL, BASE_API_PORT, _apiUrl.substr(strlen(BASE_API_URL)).c_str());
#else
        printf("POST REQUEST: %s\n", _apiUrl.c_str());
#endif
        printf("POST DATA: %s\n", _postData.c_str());
        fflush(stdout);
#endif
        // send the request
        res = curl_easy_perform(curl);

        if(res != CURLE_OK /* request failed */) {
//            curl_slist_free_all(headers); // might produce future errors? keep an eye on
            curl_easy_cleanup(curl);
            return std::make_pair<bool, std::string>(false, std::string(curl_easy_strerror(res)));
        }

        // request succeeded
//        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        return std::make_pair<bool, std::string>(true, std::move(data));
    }, std::move(std::string(BASE_API_URL) + "/api" + apiUrl), std::move(postData));
}

bool CountButton::draw(int x, int y, int r) {
    constexpr int fontSize = 36;
    if(!m_isCounting)
        if(isHover(x, y, r)) {
            DrawCircle(x, y, r, ColorTint(m_buttonColorInitial, GRAY));
            DrawCircle(x, y, r - 20, ColorTint(m_buttonColorInitial, LIGHTGRAY));
            DrawText(m_startText, x - (MeasureText(m_startText, fontSize) / 2), y - (fontSize / 2), fontSize, LIGHTGRAY);
        } else {
            DrawCircle(x, y, r, ColorTint(m_buttonColorInitial, LIGHTGRAY));
            DrawCircle(x, y, r - 20, m_buttonColorInitial);
            DrawText(m_startText, x - (MeasureText(m_startText, fontSize) / 2), y - (fontSize / 2), fontSize, WHITE);
        }
    else
        if(isHover(x, y, r)) {
            DrawCircle(x, y, r, ColorTint(m_buttonColorCounting, GRAY));
            DrawCircle(x, y, r - 20, ColorTint(m_buttonColorCounting, LIGHTGRAY));
            DrawText(m_stopText, x - (MeasureText(m_stopText, fontSize) / 2), y - (fontSize / 2), fontSize, LIGHTGRAY);
        } else {
            DrawCircle(x, y, r, ColorTint(m_buttonColorCounting, LIGHTGRAY));
            DrawCircle(x, y, r - 20, m_buttonColorCounting);
            DrawText(m_stopText, x - (MeasureText(m_stopText, fontSize) / 2), y - (fontSize / 2), fontSize,
                     WHITE);
        }

    return isHover(x, y, r) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

void CountButton::toggleCounting() {
    m_isCounting = !m_isCounting;
}

bool CountButton::isCounting() const {
    return m_isCounting;
}

int DrawCreateNewTable(char* buf, size_t maxlen) {
    GuiPanel(Rectangle {10.f, 10.f, 600.f - 20.f, 800.f - 20.f}, "New Track");
    static bool selected = true;
    auto textBounds = Rectangle {60.f, 45.f, 150.f, 50.f};
    if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if(CheckCollisionPointRec(GetMousePosition(), textBounds)) {
            selected = true;
        } else {
            selected = false;
        }
    }

    GuiTextBox(textBounds, buf, maxlen, selected);

    if(GuiButton(Rectangle{60.f, 125.f, 75.f, 50.f}, "Cancel")) return 1;

    if(GuiButton(Rectangle{150.f, 125.f, 75.f, 50.f}, "Create")) return 2;

    return 0;
}

void DrawProjectPicker(ApplicationDetails& details) {
    static bool promptNewTable = false;
    static std::vector<char> newTableBuf(256);

    if(promptNewTable) {
        int result = DrawCreateNewTable(newTableBuf.data(), 255);
        promptNewTable = result == 0;
        if(result == 2) {
            // Create the table
            std::string trackName(newTableBuf.data());
            details.apicall = MakeAPICall("/new",
                                          "track=" + trackName + "&uid=" + std::to_string(details.auth.userid));
            details.tracksCached = false;
        }
        return;
    }

    auto& tracks = details.trackNames;
    Rectangle trackBounds = {0.f, 0.f, 300.f, 30.f};
    Rectangle editBounds = {trackBounds.width + 5.f, 0.f, 45.f, trackBounds.height};
    Rectangle deleteBounds = {trackBounds.width + editBounds.width + 10.f, 0.f, 45.f, trackBounds.height};

    // https://github.com/raysan5/raygui/blob/master/examples/scroll_panel/scroll_panel.c
    // bounds is the size of the control on screen, content is the size of the inner content you are going to draw, Scroll is a pointer to a vector to store the current offset from the bounds to the content, and view is a pointer to the rectangle you would use to clip the content when you draw it later (with BeginScissor)
    // scroll => GuiScrollPanel will set the data in it based on input
    Rectangle contentBounds = {0.f, 0.f, trackBounds.width + editBounds.width + deleteBounds.width + 25.f, ((trackBounds.height + 5.f) * tracks.size()) + 10.f};
    static Vector2 scroll = {0}; // TODO: Reset value upon option selection
    static Rectangle view = {0}; // TODO: Reset value upon option selection
    GuiScrollPanel(Rectangle {10.f, 10.f, 600.f - 20.f, 800.f - 20.f}, "Pick a track", contentBounds, &scroll, &view);

    BeginScissorMode(view.x, view.y, view.width, view.height);
    for(auto i = 0UL; i < tracks.size(); i++) {
        auto bounds = trackBounds;
        bounds.x += scroll.x + 15.f;
        bounds.y += ((trackBounds.height + 5.f) * (i + 1)) + scroll.y + 10.f;
        if(CheckCollisionRecs(view, bounds)) { // only render if in bounds (saves the GPU)
            // Track button
            if(GuiButton(bounds, tracks[i].c_str())) {
                printf("User selected track #%d\n", i + 1);
                details.trackName = tracks[i];
                details.apicall = MakeAPICall("/count",
                                      "track=" + details.trackName + "&uid=" + std::to_string(details.auth.userid));
            }

            // Edit button
            bounds.x += editBounds.x;
            bounds.width = editBounds.width;
            GuiDisable();
            if (GuiButton(bounds, "Edit")) {
                printf("User selected EDIT track #%d\n", i + 1);
            }
            GuiEnable();

            // Delete button
            bounds.x = bounds.x - editBounds.x + deleteBounds.x;
            bounds.width = deleteBounds.width;
            if(GuiButton(bounds, "Delete")) {
                details.apicall = MakeAPICall("/delete",
                                              "track=" + tracks[i] + "&uid=" + std::to_string(details.auth.userid));
                tracks.erase(tracks.begin() + i);
            }
        }
    }
    EndScissorMode();

    if(GuiButton({10.f + 600.f - 20.f - 130.f, 12.f, 125.f, 20.f}, "New Track")) {
        promptNewTable = true;
        memset(newTableBuf.data(), 0, 255);
    }

}

void DrawLogin(APIResult * apicall) {
    constexpr unsigned long maxsize = 50;
    static char username[maxsize] = {0};
    static char password[maxsize] = {0};
    static int selected = -1;

    DrawText("Log in or Register", 300 - (MeasureText("Log in or Register", 36) / 2), 75, 36, WHITE);

    constexpr float formWidth = 160.f, formHeight = 195.f;
    float x = 300.f - (formWidth / 2.f), y = 400.f - (formHeight / 2.f) - 50.f;


    GuiGroupBox(Rectangle {x, y, 160.f, 65.f}, "Username");
    Rectangle usernameBounds = Rectangle {x + 5.f, y + 10.f, 150.f, 50.f};
    GuiGroupBox(Rectangle {x, y + 75.f, 160.f, 65.f}, "Password");
    Rectangle passwordBounds = Rectangle {x + 5.f, y + 85.f, 150.f, 50.f};

    if(CheckCollisionPointRec(GetMousePosition(), usernameBounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        selected = 0;
    } else if(CheckCollisionPointRec(GetMousePosition(), passwordBounds) && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        selected = 1;
    } else if(IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) selected = -1;

    GuiTextBox(usernameBounds, username, maxsize - 1, selected == 0 ? 1 : 0);
    GuiTextBox(passwordBounds, password, maxsize - 1, selected == 1 ? 1 : 0);

    bool apiCallOngoing = apicall->valid();

    if(apiCallOngoing || username[0] == 0 || password[0] == 0) GuiDisable();
    if(GuiButton(Rectangle {x, y + 145.f, 75.f, 50.f}, "Log in")) {
        std::string user = username;
        std::string pass = password;

        memset(username, 0, maxsize);
        memset(password, 0, maxsize);

        printf("Username: %s\n", user.c_str());
        printf("Password: %s\n", pass.c_str());

        {
            CURL* curl = curl_easy_init();
            if(curl) {
                char* userSanitized = curl_easy_escape(curl, user.c_str(), user.size());
                char* passSanitized = curl_easy_escape(curl, pass.c_str(), pass.size());
                user = std::string(userSanitized);
                pass = std::string(passSanitized);
                curl_free(userSanitized);
                curl_free(passSanitized);
            } else throw std::runtime_error("Failed to initialize CURL.");
            curl_easy_cleanup(curl);
        }

        *apicall = MakeAPICall("/login", "username=" + user +"&password=" + pass);
    }
    if(GuiButton(Rectangle {x + 85.f, y + 145.f, 75.f, 50.f}, "Register")) {
        std::string user = username;
        std::string pass = password;

        memset(username, 0, maxsize);
        memset(password, 0, maxsize);

        printf("Username: %s\n", user.c_str());
        printf("Password: %s\n", pass.c_str());

        {
            CURL* curl = curl_easy_init();
            if(curl) {
                char* userSanitized = curl_easy_escape(curl, user.c_str(), user.size());
                char* passSanitized = curl_easy_escape(curl, pass.c_str(), pass.size());
                user = std::string(userSanitized);
                pass = std::string(passSanitized);
                curl_free(userSanitized);
                curl_free(passSanitized);
            } else throw std::runtime_error("Failed to initialize CURL.");
            curl_easy_cleanup(curl);
        }

        *apicall = MakeAPICall("/register", "username=" + user +"&password=" + pass);
    }
    GuiEnable();
}

std::string SecondsToHMS(uint64_t seconds) {
    auto hh = std::chrono::duration_cast<std::chrono::hours>(std::chrono::seconds(seconds));
    auto mm = std::chrono::duration_cast<std::chrono::minutes>(std::chrono::seconds(seconds) - hh);
    auto ss = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::seconds(seconds) - hh - mm);

    if(hh.count() > 0ULL) {
        // HH:MM:SS
        std::string s_ss = ss.count() > 9ULL ? std::to_string(ss.count()) : "0" + std::to_string(ss.count());
        std::string s_mm = mm.count() > 9ULL ? std::to_string(mm.count()) : "0" + std::to_string(mm.count());
        std::string s_hh = std::to_string(hh.count());

        return s_hh + ":" + s_mm + ":" + s_ss;
    } else if(mm.count() > 0ULL) {
        // MM:SS
        std::string s_ss = ss.count() > 9ULL ? std::to_string(ss.count()) : "0" + std::to_string(ss.count());
        std::string s_mm = std::to_string(mm.count());

        return s_mm + ":" + s_ss;
    } else {
        // SS
        std::string s_ss = std::to_string(ss.count());

        return s_ss;
    }
}