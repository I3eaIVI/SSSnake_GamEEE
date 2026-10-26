/*
 * Snake game for Arduino UNO R4 WiFi (built-in 12x8 LED matrix)
*/

#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"
#include <WiFiS3.h>

// ------------------------- user config ------------------------- //

struct Pin {
	static const short joystickX = A2;
	static const short joystickY = A3;
	static const short potentiometer = A5; // snake speed control
};

const short initialSnakeLength = 3; // 1...20, recommended 3
const int joystickThreshold = 160;  // joystick dead zone

const char FIREBASE_HOST[] = "sssnake-gameee-default-rtdb.asia-southeast1.firebasedatabase.app";
const char SECRET_SSID[] = "ชื่อ-wifi";
const char SECRET_PASS[] = "รหัสผ่าน-wifi";
// ------------------------- variables --------------------------- //

const int ROWS = 8;
const int COLS = 12;

ArduinoLEDMatrix matrix;
byte frame[ROWS][COLS] = {}; // what is shown on the matrix

struct Point {
	int row = 0, col = 0;
	Point(int row = 0, int col = 0): row(row), col(col) {}
};

struct Coordinate {
	int x = 0, y = 0;
	Coordinate(int x = 0, int y = 0): x(x), y(y) {}
};

bool win = false;
bool gameOver = false;

Point snake;
Point food(-1, -1);
Coordinate joystickHome(500, 500);

int snakeLength = initialSnakeLength;
int snakeSpeed = 100;   // delay between steps in ms, set by the potentiometer
int snakeDirection = 0; // 0 = not moving

const short up = 1;
const short right = 2;
const short down = 3; // 'down - 2' must be 'up'
const short left = 4; // 'left - 2' must be 'right'

int gameboard[ROWS][COLS] = {}; // snake body segments

// ------------------------- main -------------------------------- //

void setup() {
	Serial.begin(115200);
	analogReadResolution(10); // same 0...1023 range as the classic Uno
	matrix.begin();
	randomSeed(analogRead(A4));
	snake.row = random(ROWS);
	snake.col = random(COLS);
	calibrateJoystick(); // do not touch the joystick now
	scrollText("SNAKE");
	connectWiFi(); // the game still works offline if this fails
}

void loop() {
	generateFood();
	scanJoystick();
	calculateSnake();
	handleGameStates();
}

// ------------------------- display ----------------------------- //

void render(bool foodOn) {
	for (int r = 0; r < ROWS; r++) {
		for (int c = 0; c < COLS; c++) {
			frame[r][c] = gameboard[r][c] > 0 ? 1 : 0;
		}
	}
	if (foodOn && food.row >= 0) frame[food.row][food.col] = 1;
	matrix.renderBitmap(frame, ROWS, COLS);
}

void scrollText(const String &text) {
	matrix.beginDraw();
	matrix.stroke(0xFFFFFFFF);
	matrix.textScrollSpeed(60);
	matrix.textFont(Font_5x7);
	matrix.beginText(0, 1, 0xFFFFFF);
	matrix.print("   " + text + "   ");
	matrix.endText(SCROLL_LEFT);
	matrix.endDraw();
}

// ------------------------- game logic -------------------------- //

void generateFood() {
	if (food.row == -1 || food.col == -1) {
		if (snakeLength >= ROWS * COLS) {
			win = true;
			return;
		}
		do {
			food.col = random(COLS);
			food.row = random(ROWS);
		} while (gameboard[food.row][food.col] > 0);
	}
}

bool joystickMoved() {
	int x = analogRead(Pin::joystickX);
	int y = analogRead(Pin::joystickY);
	return y < joystickHome.y - joystickThreshold || y > joystickHome.y + joystickThreshold
	       || x < joystickHome.x - joystickThreshold || x > joystickHome.x + joystickThreshold;
}

// reads joystick + potentiometer for one game step and blinks the food
void scanJoystick() {
	int previousDirection = snakeDirection;
	unsigned long timestamp = millis();
	bool lastFoodState = false;
	render(lastFoodState);

	while (millis() < timestamp + snakeSpeed) {
		// speed: exponential curve, 10...1000 ms per step
		float raw = analogRead(Pin::potentiometer) / 1023.0;
		snakeSpeed = 10 + (int)(pow(raw, 3.5) * 990);

		int x = analogRead(Pin::joystickX);
		int y = analogRead(Pin::joystickY);
		if (y < joystickHome.y - joystickThreshold) snakeDirection = up;
		if (y > joystickHome.y + joystickThreshold) snakeDirection = down;
		if (x < joystickHome.x - joystickThreshold) snakeDirection = left;
		if (x > joystickHome.x + joystickThreshold) snakeDirection = right;

		// ignore a 180 degree turn
		if (previousDirection != 0 && abs(snakeDirection - previousDirection) == 2) {
			snakeDirection = previousDirection;
		}

		bool foodState = millis() % 100 < 50;
		if (foodState != lastFoodState) { // redraw only when the blink changes
			lastFoodState = foodState;
			render(foodState);
		}
	}
}

void calculateSnake() {
	switch (snakeDirection) {
		case up:    snake.row--; break;
		case right: snake.col++; break;
		case down:  snake.row++; break;
		case left:  snake.col--; break;
		default: return; // not moving yet
	}
	fixEdge();

	// hit own body
	if (gameboard[snake.row][snake.col] > 1) {
		gameOver = true;
		return;
	}

	// food eaten
	if (snake.row == food.row && snake.col == food.col) {
		food.row = -1;
		food.col = -1;
		snakeLength++;
		for (int r = 0; r < ROWS; r++) {
			for (int c = 0; c < COLS; c++) {
				if (gameboard[r][c] > 0) gameboard[r][c]++;
			}
		}
	}

	gameboard[snake.row][snake.col] = snakeLength + 1;

	for (int r = 0; r < ROWS; r++) {
		for (int c = 0; c < COLS; c++) {
			if (gameboard[r][c] > 0) gameboard[r][c]--;
		}
	}
}

// snake appears on the other side when it leaves the screen
void fixEdge() {
	if (snake.col < 0) snake.col += COLS;
	if (snake.col >= COLS) snake.col -= COLS;
	if (snake.row < 0) snake.row += ROWS;
	if (snake.row >= ROWS) snake.row -= ROWS;
}

void handleGameStates() {
	if (!(gameOver || win)) return;

	int score = snakeLength - initialSnakeLength;

	// flash the screen 5 times
	for (int i = 0; i < 5; i++) {
		for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) frame[r][c] = gameboard[r][c] == 0;
		matrix.renderBitmap(frame, ROWS, COLS);
		delay(100);
		render(false);
		delay(100);
	}

	scrollText(gameOver ? "GAME OVER" : "YOU WIN");
	scrollText("SCORE " + String(score));

	submitScore(score); // send the score to the online leaderboard

	// reset the game
	win = false;
	gameOver = false;
	snake.row = random(ROWS);
	snake.col = random(COLS);
	food.row = -1;
	food.col = -1;
	snakeLength = initialSnakeLength;
	snakeDirection = 0;
	memset(gameboard, 0, sizeof(gameboard));
	render(false);

	while (joystickMoved()) {} // wait for the joystick to come back
}

// ------------------------- leaderboard ------------------------- //

bool connectWiFi() {
	if (WiFi.status() == WL_NO_MODULE) return false;
	if (WiFi.status() == WL_CONNECTED) return true;

	Serial.print("Connecting to WiFi...");
	unsigned long start = millis();
	while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
		WiFi.begin(SECRET_SSID, SECRET_PASS);
		delay(1000);
	}
	Serial.println(WiFi.status() == WL_CONNECTED ? " connected" : " failed");
	return WiFi.status() == WL_CONNECTED;
}

// sends the score to Firebase: POST /scores.json  {"score": N, "ts": <server time>}
void submitScore(int score) {
	Serial.print("Score: ");
	Serial.println(score);
	if (score <= 0) return; // do not post empty games

	if (!connectWiFi()) {
		Serial.println("No WiFi, score not sent");
		return;
	}

	WiFiSSLClient client;
	if (!client.connect(FIREBASE_HOST, 443)) {
		Serial.println("Cannot reach Firebase, score not sent");
		return;
	}

	String body = "{\"score\":" + String(score) + ",\"ts\":{\".sv\":\"timestamp\"}}";
	client.println("POST /scores.json HTTP/1.1");
	client.print("Host: ");
	client.println(FIREBASE_HOST);
	client.println("Content-Type: application/json");
	client.print("Content-Length: ");
	client.println(body.length());
	client.println("Connection: close");
	client.println();
	client.print(body);

	// read the status line (expect "HTTP/1.1 200 OK")
	unsigned long t = millis();
	while (client.connected() && !client.available() && millis() - t < 5000) {}
	if (client.available()) Serial.println(client.readStringUntil('\n'));
	client.stop();
}

void calibrateJoystick() {
	long sx = 0, sy = 0;
	for (int i = 0; i < 10; i++) {
		sx += analogRead(Pin::joystickX);
		sy += analogRead(Pin::joystickY);
		delay(5);
	}
	joystickHome.x = sx / 10;
	joystickHome.y = sy / 10;
}
