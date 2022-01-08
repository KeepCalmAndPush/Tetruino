//
// This program requires a Nokia 5110 LCD module.
//
// It is assumed that the LCD module is connected to
// the following pins using a levelshifter to get the
// correct voltage to the module.
//      SCK  - Pin 8
//      MOSI - Pin 9
//      DC   - Pin 10
//      RST  - Pin 11
//      CS   - Pin 12
//
#include <LCD5110_Graph.h>
/* Uncomment for DEBUG
#include <MemoryFree.h>
*/

#define fromZeroTo(name, max) for(int name = 0; name < max; name++)
#define throughZeroFrom(name, max) for(int name = max; name >= 0; name--)
#define MAKE_BOOL_ARRAY_NAMED(arr, r, c) bool *arr = calloc((r * c), sizeof(bool));
#define p(value) Serial.print(value)
#define ps(value) p(value); p(" ")
#define pl(value) Serial.println(value)
#define FOREACH_PIXEL_IN_POINT(point, block)\
  {\
    int origin_x = point.x * PIXELS_IN_POINT + GLASS_BORDER_WIDTH;\
    int origin_y = point.y * PIXELS_IN_POINT + GLASS_BORDER_WIDTH + GLASS_INSET_Y;\
    \
    fromZeroTo (dx, PIXELS_IN_POINT) { fromZeroTo (dy, PIXELS_IN_POINT) {\
        int x = origin_x + dx;\
        int y = origin_y + dy + 1;\
        \
        block\
      }\
    }\
  }

void drawFigure(Figure* figure, DrawMode mode);

typedef struct {
  int x;
  int y;
} Point;

typedef struct {
  int cols;
  int rows;
} Size;

typedef struct {
  bool *data;
  Point origin;
  Size size;
} Figure;

Figure* Figure_new(bool *_data, Point _origin, Size _size) {
  Figure* figure = malloc(sizeof(Figure));
  figure->data = _data;
  figure->origin = _origin;
  figure->size = _size;
  return figure;
}

typedef enum {
  filled,
  wireframe,
  cleared
} DrawMode;

LCD5110 myGLCD(8, 9, 10, 11, 12);
extern uint8_t TinyFont[];

const int PAUSE_BUTTON = 2;
const int ROTATE_BUTTON = 3;
const int RIGHT_BUTTON = 4;
const int LEFT_BUTTON = 5;

const int PIXELS_IN_POINT = 4;
const int POINTS_IN_FIGURE = 4;

const int SCREEN_WIDTH = 84;
const int SCREEN_HEIGHT = 48;

const int GLASS_BORDER_WIDTH = 1;
const int GLASS_WIDTH = SCREEN_HEIGHT + GLASS_BORDER_WIDTH;
const int GLASS_HEIGHT = 36 + 2 * GLASS_BORDER_WIDTH;
const int GLASS_INSET_Y = 0;
const int GLASS_LEFT_BORDER = 0;
const int GLASS_RIGHT_BORDER = GLASS_WIDTH;

const int ROWS_COUNT = (GLASS_HEIGHT - 2 * GLASS_BORDER_WIDTH) / PIXELS_IN_POINT;
const int COLS_COUNT = (GLASS_WIDTH - GLASS_BORDER_WIDTH) / PIXELS_IN_POINT;

bool *DeadPoints = calloc(ROWS_COUNT*COLS_COUNT, sizeof(bool));

Figure *CurrentFigure = NULL;
Figure *NextFigure = NULL;

int Score = 0;

void setup() {
  myGLCD.InitLCD();
  myGLCD.setFont(TinyFont);
  Serial.begin(9600);

  randomSeed(analogRead(0));
  
  prepareFigures();
}

void loop() {
  for (int b = 0; b < 5; b++) {
    handleButtons();
    myGLCD.clrScr();
    printScore();
    drawGlass();
    drawDead();
    drawFigure(CurrentFigure, wireframe);
    drawFigure(NextFigure, wireframe);
    myGLCD.update();

    delay(40);
  }
  
  moveFigureDown(CurrentFigure);
  clampIfPossible();
  handleGameOver();

/* Uncomment for DEBUG
  p("freeMemory()=");
  pl(freeMemory());
*/
}

int prevRows = 0;
int prevCols = 0;

Figure* generateFigure() {
  int upperBound = POINTS_IN_FIGURE + 1;
  int rows = 1;
  int cols = POINTS_IN_FIGURE;

  while (true) {
    rows = random(1, upperBound);
    int minCols = POINTS_IN_FIGURE - rows;
    int maxCols = (upperBound + 1) - rows;
    cols = random(minCols, maxCols);

    if (random(1, 5) % 4 != 0) {
      if (rows <= POINTS_IN_FIGURE / 4 || cols <= POINTS_IN_FIGURE / 4) {
        continue;
      }
    }
    
    if (prevCols == cols && prevRows == rows) {
      continue;
    }

    if (cols * rows >= POINTS_IN_FIGURE) {
      prevCols = cols;
      prevRows = rows;
      break;
    }
  }

  int length = rows * cols;
  while (true) {
    int seed;
    int max = (1 << length);
    if (rows * cols == POINTS_IN_FIGURE) {
      seed = random(max - 1, max);
    } else {
      seed = random(0, max);
    }

    MAKE_BOOL_ARRAY_NAMED(data, rows, cols);
    intToBin(seed, length, data);

    if (isDataValid(data, {cols, rows})) {
      return Figure_new(data, {0, 0}, {cols, rows});
    }

    free(data);
  }
}

void prepareFigures() {
  if (CurrentFigure == NULL && NextFigure == NULL) {
    CurrentFigure = generateFigure();
    NextFigure = generateFigure();
  } else {
    free(CurrentFigure->data);
    free(CurrentFigure);
    CurrentFigure = NextFigure;
    NextFigure = generateFigure();
  }

  CurrentFigure->origin = { COLS_COUNT / 2, -CurrentFigure->size.rows};
  NextFigure->origin = { COLS_COUNT + 1 , 0 };
  fixOverbounds(CurrentFigure);
}

void moveFigureDown(Figure* f) {
  moveFigureIfPossible(f, 0, 1);
}

void drawPoint(Point point, DrawMode mode = wireframe) {
  switch (mode) {
    case cleared:
      FOREACH_PIXEL_IN_POINT(point, { myGLCD.invPixel(x, y); })
      break;
    case filled:
      FOREACH_PIXEL_IN_POINT(point, { myGLCD.setPixel(x, y); })
      break;
    case wireframe:
      int x = point.x * PIXELS_IN_POINT + GLASS_BORDER_WIDTH;
      int y = point.y * PIXELS_IN_POINT + GLASS_BORDER_WIDTH + GLASS_INSET_Y;
      drawRect(x, y, x + PIXELS_IN_POINT, y + PIXELS_IN_POINT);
      break;
  }
}

void showRect(Point origin, Size size, DrawMode mode = filled) {
  MAKE_BOOL_ARRAY_NAMED(data, size.cols, size.rows)
  fromZeroTo (i, size.cols * size.rows) {
    data[i] = true;
  }
  Figure *figure = Figure_new(data, origin, size);
  drawFigure(figure, mode);
  free(data);
  free(figure);
}

void drawFigure(Figure* figure, DrawMode mode = wireframe) {
  int x = figure->origin.x;
  int y = figure->origin.y;
  fromZeroTo (r, figure->size.rows) {
    fromZeroTo (c, figure->size.cols) {
      if (figure->data[c + (r * figure->size.cols)] == true) {
        Point point = {c + x, r + y};
        drawPoint(point, mode);
      }
    }
  }
}

void addToDead(Figure* figure) {
  int x = figure->origin.x;
  int y = figure->origin.y;
  fromZeroTo (r, figure->size.rows) {
    fromZeroTo (c, figure->size.cols) {
      if (figure->data[c + (r * figure->size.cols)] == true) {
        int real_x = c + x;
        int real_y = r + y;
        int index = real_x + COLS_COUNT * real_y;
        DeadPoints[index] = true;
      }
    }
  }

  drawDead();
}

void drawDead() {
  Figure *dead = Figure_new(DeadPoints, {0, 0}, {COLS_COUNT, ROWS_COUNT});
  drawFigure(dead, filled);
  free(dead);
}

void drawGlass() {
  drawRect(GLASS_LEFT_BORDER,
           GLASS_INSET_Y,
           GLASS_RIGHT_BORDER,
           GLASS_HEIGHT + GLASS_INSET_Y);
}

void drawRect(int x1, int y1, int x2, int y2) {
  myGLCD.drawRect(x1, y1, x2, y2);
}

void handleButtons() {
  if (isButtonPressed(LEFT_BUTTON)) {
    moveFigureIfPossible(CurrentFigure, -1, 0);
  } else if (isButtonPressed(RIGHT_BUTTON)) {
    moveFigureIfPossible(CurrentFigure, 1, 0);
  } else if (isButtonPressed(ROTATE_BUTTON)) {
    rotateFigure(CurrentFigure);
  } else if (isButtonPressed(PAUSE_BUTTON)) {
    moveFigureIfPossible(CurrentFigure, 0, 1);
  }
}

void rotateFigure(Figure* figure) {
  int cols = figure->size.cols;
  int rows = figure->size.rows;

  bool *oldData = figure->data;
  MAKE_BOOL_ARRAY_NAMED(newData, cols, rows);
  fromZeroTo (r, rows) {
    fromZeroTo (c, cols) {
      int value = oldData[c + r * cols];
      int row = rows - r - 1;
      newData[row + c * rows] = value;
    }
  }

  Figure *f = Figure_new(newData, figure->origin, {rows, cols});
  if (willFigureIntersectWithDead(f, 0, 0)) {
    free(newData);
    free(f);
    return;
  }
  
  free(f);
  free(oldData);
  figure->data = newData;
  figure->size = { rows, cols };
  fixOverbounds(figure);
}

void fixOverbounds(Figure *figure) {
  char xOverbounds = COLS_COUNT - figure->origin.x - figure->size.cols;
  if (xOverbounds < 0) {
    figure->origin.x += xOverbounds;
  }

  char yOverbounds = ROWS_COUNT - figure->origin.y - figure->size.rows;
  if (yOverbounds < 0) {
    figure->origin.y += yOverbounds;
  }
}

void handleGameOver() {
  bool isGameOver = false;
  fromZeroTo (c, COLS_COUNT) {
    if (intInAt(DeadPoints, 0, c, COLS_COUNT) == true) {
      isGameOver = true;
      break;
    }
  }

  if (!isGameOver) {
    return;
  }

  Score = 0;

  free(DeadPoints);
  DeadPoints = calloc(ROWS_COUNT * COLS_COUNT, sizeof(bool));
  prepareFigures();
}

void clampIfPossible() {
  byte* rowsToClamp = calloc(ROWS_COUNT, sizeof(byte));
  int clampCount = 0;

  throughZeroFrom(r, ROWS_COUNT - 1) {
    fromZeroTo (c, COLS_COUNT) {
      if (DeadPoints[c + (r * COLS_COUNT)] == 0) {
        goto nextRow;
      }
    }
    rowsToClamp[clampCount] = r;
    clampCount++;
    Score += 100 * clampCount;
nextRow:;
  }

  if (clampCount == 0) {
    free(rowsToClamp);
    return;
  }

  fromZeroTo (i, 5) {
    DrawMode mode = i % 2 ? filled : cleared;
    fromZeroTo (r, clampCount) {
      byte row = rowsToClamp[r];
      showRect({0, row}, {COLS_COUNT, 1}, mode);
    }
    myGLCD.update();
    delay(100);
  }

  bool *oldData = DeadPoints;
  bool *newData = calloc(ROWS_COUNT * COLS_COUNT, sizeof(bool));
  int oldRowPointer = ROWS_COUNT - 1;
  int newRowPointer = ROWS_COUNT - 1;
  throughZeroFrom(r, ROWS_COUNT - 1) {
    if (contains(rowsToClamp, clampCount, r)) {
      oldRowPointer--;
    } else {
      fromZeroTo (c, COLS_COUNT) {
        int oldValue = false;
        int oldIndex = c + (oldRowPointer * COLS_COUNT);
        if (oldIndex > 0) {
          oldValue = oldData[oldIndex];
        }
        int newIndex = c + (newRowPointer * COLS_COUNT);
        newData[newIndex] = oldValue;
      }
      newRowPointer--;
      oldRowPointer--;
    }
  }

  DeadPoints = newData;
  free(rowsToClamp);
  free(oldData);
}

byte contains(byte *arr, int count, byte element) {
  fromZeroTo (i, count) {
    if (arr[i] == element) {
      return true;
    }
  }
  return false;
}

void printScore() {
  int x = GLASS_WIDTH + 2;
  int y = GLASS_INSET_Y + POINTS_IN_FIGURE * PIXELS_IN_POINT + 3;
  myGLCD.printNumI(Score, x, y, 6, '0');
}

boolean isButtonPressed(int pin) {
  boolean keyUp = digitalRead(pin);
  delay(20);
  Serial.write(pin);
  return !keyUp;
}

int intInAt(bool* data, int r, int c, int cols) {
  return data[c + (r * cols)];
}

bool isDataValid(bool *data, Size size) {
  int rows = size.rows;
  int cols = size.cols;
  int numberOfOnes = 0;

  fromZeroTo (r, rows) {
    int prevRow = max(0, r - 1);
    int nextRow = min(rows - 1, r + 1);
    fromZeroTo (c, cols) {
      int prevCol = max(0, c - 1);
      int nextCol = min(cols - 1, c + 1);
      int value = intInAt(data, r, c, cols);
      if (value == true) {
        if (++numberOfOnes > POINTS_IN_FIGURE) {
          return false;
        }

        bool noNeighbors = true;
        if (prevRow != r) {
          noNeighbors &= (intInAt(data, prevRow, c, cols) == false);
        }
        if (nextRow != r) {
          noNeighbors &= (intInAt(data, nextRow, c, cols) == false);
        }
        if (prevCol != c) {
          noNeighbors &= (intInAt(data, r, prevCol, cols) == false);
        }
        if (nextCol != c) {
          noNeighbors &= (intInAt(data, r, nextCol, cols) == false);
        }
        if (noNeighbors) {
          return false;
        }
      }
    }
  }

  if (numberOfOnes != POINTS_IN_FIGURE) {
    return false;
  }

  fromZeroTo(r, size.rows) {
    fromZeroTo(c, size.cols) {
      if (data[c + r * size.cols] == true) {
        goto nextRow;
      }
    }
    return false;
nextRow:;
  }

  fromZeroTo(c, size.cols) {
    fromZeroTo(r, size.rows) {
      if (data[c + r * size.cols] == true) {
        goto nextCol;
      }
    }
    return false;
nextCol:;
  }

  return true;
}

void moveFigureIfPossible(Figure* figure, int dx, int dy) {
  if (!willFigureStayInGlassBounds(figure, dx, dy)) {
    if  (dy > 0) {
      addToDead(figure);
      prepareFigures();
      return;
    }
    dx = 0;
  }

  if (willFigureIntersectWithDead(figure, dx, dy)) {
    if  (dy > 0) {
      addToDead(figure);
      prepareFigures();
      return;
    }
    dx = 0;
  }

  figure->origin.x += dx;
  figure->origin.y += dy;
}

bool willFigureStayInGlassBounds(Figure* figure, int dx, int dy) {
  Size size = figure->size;
  Point origin = figure->origin;

  bool mayMove = true;

  if (dx < 0) {
    mayMove &= (origin.x + dx >= 0);
  }
  if (dx > 0) {
    mayMove &= (origin.x + dx + size.cols <= COLS_COUNT);
  }
  if (dy < 0) {
    mayMove &= (origin.y + dy >= 0);
  }
  if (dy > 0) {
    mayMove &= (origin.y + dy + size.rows <= ROWS_COUNT);
  }

  return mayMove;
}

bool willFigureIntersectWithDead(Figure* figure, int dx, int dy) {
  int origin_x = figure->origin.x;
  int origin_y = figure->origin.y;
  Size size = figure->size;

  fromZeroTo (r, figure->size.rows) {
    fromZeroTo (c, figure->size.cols) {
      if (figure->data[c + (r * size.cols)] == false) {
        continue;
      }
      int dead_x = min(max(0, c + origin_x + dx), COLS_COUNT - 1);
      int dead_y = min(max(0, r + origin_y + dy), ROWS_COUNT - 1);
      int index = dead_y * (COLS_COUNT) + dead_x;
      if (DeadPoints[index] == true) {
        return true;
      }
    }
  }
  return false;
}

void intToBin(int in, int count, bool* out) {
  int mask = 1U << (count - 1);
  int i;
  for (i = 0; i < count; i++) {
    out[i] = (in & mask) ? true : false;
    in <<= 1;
  }
}

/* Uncomment for DEBUG
void printArr(bool * arr, Size size) {
  int rows = size.rows;
  int cols = size.cols;
  p("DATA\nrows: "); p(rows); p(" cols: "); pl(cols);
  fromZeroTo (i, rows * cols) {
    p(arr[i]);
  }
  fromZeroTo (r, rows) {
    fromZeroTo (c, cols) {
      int value = arr[c + (r * cols)];
      p(value);
    }
    pl();
  }
  pl();
}
*/
