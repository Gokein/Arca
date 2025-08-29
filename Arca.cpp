#include <windowsx.h>
#include "framework.h"
#include "Arca.h"
#include <objidl.h>
#include <gdiplus.h>
#include <unordered_map>
#include <memory>
#include <string>
#include <algorithm> 
#include <ctime>

using namespace Gdiplus;
using namespace std;

#pragma comment(lib, "gdiplus.lib")

#define MAX_LOADSTRING 100
#define WIN32_LEAN_AND_MEAN  

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];

bool gamePaused = false;
bool stepFrame = false;
bool isRightMouseDown = false;
POINT mousePos;

const float PI = 3.14159265f;

class SpriteManager {
private:
    std::unordered_map<int, std::unique_ptr<Bitmap>> sprites;

public:
    ~SpriteManager() {
        sprites.clear();
    }

    bool LoadSprite(const std::wstring& path, int id) {
        if (sprites.find(id) != sprites.end()) {
            return false;
        }

        auto bitmap = std::make_unique<Bitmap>(path.c_str());
        if (bitmap->GetLastStatus() != Ok) {
            return false;
        }

        sprites[id] = std::move(bitmap);
        return true;
    }

    Bitmap* GetSprite(int id) const {
        auto it = sprites.find(id);
        return it != sprites.end() ? it->second.get() : nullptr;
    }
    void Clear() {
        sprites.clear();
    }
};

SpriteManager spriteManager;

struct Cords {
    float x, y;
};

struct Line {
    Cords p1, p2;
};

class Block {
private:
    float x, y;
    float width, height;
    bool destroyed;
    int textureId;
public:
    Line top;
    Line right;
    Line bottom;
    Line left;

    Block(float x, float y, float width, float height, int textureId)
        : x(x), y(y), width(width), height(height),
        textureId(textureId), destroyed(false)
    {
        top = { {x, y}, {x + width, y} };
        right = { {x + width, y}, {x + width, y + height} };
        bottom = { {x, y + height}, {x + width, y + height} };
        left = { {x, y}, {x, y + height} };
    }

    vector<Line> GetActiveSides() const {
        vector<Line> activeSides;
        if (!destroyed) {
            activeSides.push_back(top);
            activeSides.push_back(right);
            activeSides.push_back(bottom);
            activeSides.push_back(left);
        }
        return activeSides;
    }
    bool IsDestroyed() const {
        return destroyed;
    }
    void Destroy() {
        destroyed = true;
    }
    void Draw(Graphics& graphics) const {
        if (destroyed) return;

        if (Bitmap* sprite = spriteManager.GetSprite(textureId)) {
            graphics.DrawImage(sprite, x, y, width, height);
        }
        else {
            const Color LAB_BLOCK(255, 200, 210, 220);
            const Color LAB_BLOCK_BORDER(255, 170, 180, 190);

            SolidBrush brush(LAB_BLOCK);
            graphics.FillRectangle(&brush, x, y, width, height);

            Pen pen(LAB_BLOCK_BORDER, 1.5f);
            graphics.DrawRectangle(&pen, x, y, width, height);
        }
    }
};

class Ball {
private:
    float x, y;
    float vx, vy;
    float radius;
    int textureId;
    float nextX, nextY;
    vector<Cords> points;
    vector<Cords> criticalPoints;

public:
    Ball(float startX, float startY, float startVX, float startVY, float r, int texId)
        : x(startX), y(startY), vx(startVX), vy(startVY), radius(r), textureId(texId),
        nextX(0), nextY(0) {

        UpdateCriticalPoints();
    }

    vector<Cords> GenerateCriticalPoints(float centerX, float centerY, float radius, float angleStepDegrees) {
        vector<Cords> points;
        float movementAngle = GetMovementAngleRadians();
        float startAngle = movementAngle - PI / 2;
        float endAngle = movementAngle + PI / 2;
        float angleStep = angleStepDegrees * PI / 180.0f;

        for (float angle = startAngle; angle <= endAngle; angle += angleStep) {
            points.push_back({
                centerX + radius * cosf(angle),
                centerY + radius * sinf(angle)
                });
        }
        return points;
    }

    float GetMovementAngleRadians() {
        float angle = atan2f(vy, vx);
        if (angle < 0) {
            angle += 2 * PI;
        }
        return angle;
    }

    void UpdateCriticalPoints() {
        criticalPoints = GenerateCriticalPoints(x, y, radius, 20.0f);
    }

    void Update(float deltaTime, const vector<Block>& blocks, int clientWidth, int clientHeight) {
        if (gamePaused && !stepFrame) return;

        if (x - radius < 0) {
            x = radius;
            vx = -vx;
        }
        else if (x + radius > clientWidth) {
            x = clientWidth - radius;
            vx = -vx;
        }
        if (y - radius < 0) {
            y = radius;
            vy = -vy;
        }
        else if (y + radius > clientHeight) {
            y = clientHeight - radius;
            vy = -vy;
        }

        float remainingTime = deltaTime;
        int collisionCount = 0;
        const int maxCollisions = 4;

        while (remainingTime > 0 && collisionCount < maxCollisions) {
            nextX = x + vx * remainingTime;
            nextY = y + vy * remainingTime;

            bool collision = false;
            float minT = FLT_MAX;
            Cords collisionNormal;
            const Block* hitBlock = nullptr;

            for (const auto& point : criticalPoints) {

                float futureX = point.x + (nextX - x);
                float futureY = point.y + (nextY - y);
                Line trajectory{ {point.x, point.y}, {futureX, futureY} };

                for (const auto& block : blocks) {
                    if (block.IsDestroyed()) continue;

                    float expandedLeft = block.left.p1.x;
                    float expandedTop = block.top.p1.y;
                    float expandedRight = block.right.p1.x;
                    float expandedBottom = block.bottom.p1.y;

                    float tIn, tOut;
                    Cords normal;
                    if (LiangBarsky(expandedLeft, expandedTop, expandedRight, expandedBottom,
                        trajectory.p1.x, trajectory.p1.y, trajectory.p2.x, trajectory.p2.y,
                        tIn, tOut, normal) && tIn < minT) {
                        minT = tIn;
                        collisionNormal = normal;
                        hitBlock = &block;
                        collision = true;
                    }
                }
            }

            if (collision && minT >= 0 && minT <= 1.0f) {
                float collisionTime = minT * remainingTime;
                x += vx * collisionTime;
                y += vy * collisionTime;
                Reflect(collisionNormal);
                remainingTime -= collisionTime;
                collisionCount++;

                //if (hitBlock) const_cast<Block*>(hitBlock)->Destroy();


            }
            else {
                x = nextX;
                y = nextY;
                remainingTime = 0;
            }

            UpdateCriticalPoints();

        }
    }

    static bool LiangBarsky(float edgeL, float edgeT, float edgeR, float edgeB,
        float x0, float y0, float x1, float y1,
        float& tIn, float& tOut, Cords& normal) {

        float dx = x1 - x0;
        float dy = y1 - y0;
        tIn = 0.0f;
        tOut = 1.0f;
        bool visible = false;

        auto clip = [&](float p, float q, Cords n) {
            if (p == 0) {
                if (q < 0) return false;
            }
            else {
                float t = q / p;
                if (p < 0) {
                    if (t > tIn) {
                        tIn = t;
                        normal = n;
                    }
                }
                else {
                    if (t < tOut) tOut = t;
                }
            }
            return true;
            };

        if (clip(-dx, x0 - edgeL, { -1, 0 }) &&
            clip(dx, edgeR - x0, { 1, 0 }) &&
            clip(-dy, y0 - edgeT, { 0, -1 }) &&
            clip(dy, edgeB - y0, { 0, 1 })) {

            visible = (tIn <= tOut) && (tIn >= 0) && (tIn <= 1.0f);
        }

        return visible;
    }

    void Draw(Graphics& graphics) const {
        float drawX = x - radius;
        float drawY = y - radius;
        float diameter = 2 * radius;

        if (Bitmap* sprite = spriteManager.GetSprite(textureId)) {
            graphics.DrawImage(sprite, drawX, drawY, diameter, diameter);
        }
        else {
            const Color LAB_BALL(255, 100, 150, 200);

            SolidBrush brush(LAB_BALL);
            graphics.FillEllipse(&brush, drawX, drawY, diameter, diameter);

            SolidBrush highlight(Color(100, 255, 255, 255));
            graphics.FillEllipse(&highlight, drawX + radius * 0.3f, drawY + radius * 0.3f,
                radius * 0.4f, radius * 0.4f);
        }
    }

    vector<Line> PredictTrajectory(float deltaTime, const vector<Block>& blocks) const {
        vector<Line> trajectoryLines;


        for (const auto& point : criticalPoints) {
            Cords futurePoint = {
                point.x + vx * deltaTime,
                point.y + vy * deltaTime
            };
            trajectoryLines.push_back({ point, futurePoint });
        }
        return trajectoryLines;
    }

    float GetX() const { return x; }
    float GetY() const { return y; }
    float GetNextX() const { return nextX; }
    float GetNextY() const { return nextY; }

    void MoveTo(float targetX, float targetY) {
        x = targetX;
        y = targetY;
    }

private:
    void Reflect(const Cords& normal) {
        float length = sqrt(normal.x * normal.x + normal.y * normal.y);
        if (length > 0) {
            float nx = normal.x / length;
            float ny = normal.y / length;
            float dot = vx * nx + vy * ny;
            vx -= 2 * dot * nx;
            vy -= 2 * dot * ny;
        }
    }
};

Ball ball(100.0f, 100.0f, 800.0f, 800.0f, 25.0f, 1);
vector<Block> blocks;

ULONGLONG lastFrameTime = 0;
float deltaTime = 0.016f;
int frames = 0;
ULONGLONG lastFPSUpdate = 0;
int currentFPS = 0;

ATOM MyRegisterClass(HINSTANCE hInstance);
BOOL InitInstance(HINSTANCE, int);
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK About(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
    _In_opt_ HINSTANCE hPrevInstance,
    _In_ LPWSTR    lpCmdLine,
    _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    srand(static_cast<unsigned int>(time(nullptr)));

    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_ARCA, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    //spriteManager.LoadSprite(L"assets/ball.png", 1);
    //spriteManager.LoadSprite(L"assets/brick.jpg", 1001);
    //spriteManager.LoadSprite(L"assets/racket.jpg", 2001);

    if (!InitInstance(hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_ARCA));

    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    lastFrameTime = GetTickCount64();
    lastFPSUpdate = lastFrameTime;

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            ULONGLONG currentTime = GetTickCount64();
            //deltaTime = (currentTime - lastFrameTime) / 1000.0f;
            deltaTime = 0.016f;
            lastFrameTime = currentTime;

            frames++;
            if (currentTime - lastFPSUpdate >= 1000) {
                currentFPS = frames;
                frames = 0;
                lastFPSUpdate = currentTime;
            }

            HWND hWnd = GetActiveWindow();
            InvalidateRect(hWnd, NULL, FALSE);
            UpdateWindow(hWnd);

            Sleep(1);
        }
    }

    spriteManager.Clear();
    GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

void CreateBlocks() {
    blocks.emplace_back(200.0f, 200.0f, 200.0f, 80.0f, 1001);
    blocks.emplace_back(470.0f, 200.0f, 200.0f, 80.0f, 1001);
    blocks.emplace_back(740.0f, 200.0f, 200.0f, 80.0f, 1001);
    blocks.emplace_back(200.0f, 600.0f, 200.0f, 80.0f, 1001);
    blocks.emplace_back(470.0f, 600.0f, 200.0f, 80.0f, 1001);
    blocks.emplace_back(740.0f, 600.0f, 200.0f, 80.0f, 1001);
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ARCA));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 3);
    wcex.lpszMenuName = nullptr;
    wcex.lpszClassName = szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_POPUP | WS_VISIBLE,
        0, 0, screenWidth, screenHeight, nullptr, nullptr, hInstance, nullptr);

    if (!hWnd)
    {
        return FALSE;
    }

    SetTimer(hWnd, 1, 16, NULL);
    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE: {
        CreateBlocks();
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        HDC hdcMem = CreateCompatibleDC(hdc);
        RECT rect;
        GetClientRect(hWnd, &rect);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rect.right, rect.bottom);
        HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

        Graphics graphics(hdcMem);

        const Color LAB_BACKGROUND(255, 240, 240, 245);
        const Color LAB_TEXT(255, 80, 80, 100);
        const Color LAB_GRID(255, 220, 220, 225);

        SolidBrush bgBrush(LAB_BACKGROUND);
        graphics.FillRectangle(&bgBrush, 0, 0, rect.right, rect.bottom);

        Pen gridPen(LAB_GRID, 1.0f);
        const int gridSize = 40;
        for (int x = 0; x < rect.right; x += gridSize) {
            graphics.DrawLine(&gridPen, x, 0, x, rect.bottom);
        }
        for (int y = 0; y < rect.bottom; y += gridSize) {
            graphics.DrawLine(&gridPen, 0, y, rect.right, y);
        }

        FontFamily fontFamily(L"Consolas");
        Font font(&fontFamily, 14, FontStyleRegular, UnitPixel);
        SolidBrush textBrush(LAB_TEXT);
        PointF pointF(10.0f, 10.0f);

        wstring fpsStr = L"FPS: " + to_wstring(currentFPS);
        graphics.DrawString(fpsStr.c_str(), -1, &font, pointF, &textBrush);
        pointF.Y += 20.0f;
        wstring deltaStr = L"Delta: " + to_wstring(deltaTime);
        graphics.DrawString(deltaStr.c_str(), -1, &font, pointF, &textBrush);

        if (gamePaused) {
            pointF.Y += 40.0f;
            wstring pauseStr = L"PAUSED (Press 'P' to continue, 'S' to step)";
            graphics.DrawString(pauseStr.c_str(), -1, &font, pointF, &textBrush);
        }

        for (auto& block : blocks) {
            block.Draw(graphics);
        }

        ball.Draw(graphics);

        if (gamePaused) {
            auto trajectoryLines = ball.PredictTrajectory(deltaTime, blocks);

            Pen trajectoryPen(Color(180, 80, 120, 180), 6.0f);
            Pen collisionPen(Color(200, 200, 80, 80), 6.0f);

            for (const auto& line : trajectoryLines) {
                graphics.DrawLine(&trajectoryPen, line.p1.x, line.p1.y, line.p2.x, line.p2.y);
            }

            SolidBrush pointBrush(Color(200, 60, 100, 160));

            for (const auto& line : trajectoryLines) {
                graphics.FillEllipse(&pointBrush, (REAL)(line.p1.x - 2), (REAL)(line.p1.y - 2), (REAL)4, (REAL)4);
            }

            wstring angleStr = L"Angle: " + to_wstring(ball.GetMovementAngleRadians()) + L"°";
            pointF.Y += 20.0f;
            graphics.DrawString(angleStr.c_str(), -1, &font, pointF, &textBrush);

            SolidBrush collisionBrush(Color(220, 220, 100, 100));
            for (const auto& line : trajectoryLines) {
                for (const auto& block : blocks) {
                    if (block.IsDestroyed()) continue;

                    for (const auto& blockSide : block.GetActiveSides()) {
                        float tIn, tOut;
                        Cords normal;
                        if (Ball::LiangBarsky(blockSide.p1.x, blockSide.p1.y,
                            blockSide.p2.x, blockSide.p2.y,
                            line.p1.x, line.p1.y,
                            line.p2.x, line.p2.y,
                            tIn, tOut, normal)) {
                            float collisionX = line.p1.x + (line.p2.x - line.p1.x) * tIn;
                            float collisionY = line.p1.y + (line.p2.y - line.p1.y) * tIn;
                            graphics.FillEllipse(&collisionBrush,
                                (REAL)(collisionX - 3), (REAL)(collisionY - 3),
                                (REAL)6, (REAL)6);
                        }

                    }
                }
            }

        }

        BitBlt(hdc, 0, 0, rect.right, rect.bottom, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hbmOld);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);

        EndPaint(hWnd, &ps);
        break;
    }

    case WM_RBUTTONDOWN: {
        isRightMouseDown = true;
        mousePos.x = GET_X_LPARAM(lParam);
        mousePos.y = GET_Y_LPARAM(lParam);
        ball.MoveTo(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
        ball.UpdateCriticalPoints();
        InvalidateRect(hWnd, NULL, FALSE);
        break;        break;
    }

    case WM_MOUSEMOVE: {
        if (isRightMouseDown) {
            mousePos.x = GET_X_LPARAM(lParam);
            mousePos.y = GET_Y_LPARAM(lParam);
            ball.MoveTo(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y));
            ball.UpdateCriticalPoints();
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    }

    case WM_RBUTTONUP: {
        isRightMouseDown = false;
        break;
    }

    case WM_KEYDOWN:
        if (wParam == 'P') {
            gamePaused = !gamePaused;
        }
        else if (wParam == 'S') {
            if (gamePaused) {
                stepFrame = true;
            }
        }
        break;
    case WM_KEYUP:
        break;

    case WM_TIMER: {
        if (!gamePaused || stepFrame) {
            RECT clientRect;
            GetClientRect(hWnd, &clientRect);

            if (isRightMouseDown) {
                float targetX = static_cast<float>(mousePos.x);
                float targetY = static_cast<float>(mousePos.y);
                ball.MoveTo(targetX, targetY);
            }
            else {
                ball.Update(deltaTime, blocks, clientRect.right, clientRect.bottom);
            }

            if (stepFrame) {
                stepFrame = false;
            }
        }
        break;
    }
    case WM_DESTROY: {
        spriteManager.Clear();
        KillTimer(hWnd, 1);
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}