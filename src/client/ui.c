#include "client/ui.h"

#include <pthread.h>
#include <raylib.h>
#include <stdint.h>

#include "chess/board.h"
#include "chess/movegen.h"
#include "chess/rules.h"
#include "client/client.h"
#include "proto/proto.h"

// --- Theme ---------------------------------------------------------------------------------------

#define STATUS_BAR_HEIGHT 36
#define CHAT_MIN_WIDTH 220
#define BORDER_SIZE 20
#define ACCENT_SIZE 4
#define PIECE_SLOTS 16
#define FONT_ATLAS_SIZE 64
#define STATUS_FONT_SIZE 20

#define HEX(h) { ((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF, 0xFF }
#define HEXA(h, a) { ((h) >> 16) & 0xFF, ((h) >> 8) & 0xFF, (h) & 0xFF, (a) }
static const Color BOARD_LIGHT = HEX(0xF0D9B5);
static const Color BOARD_DARK = HEX(0xB58863);
static const Color BOARD_BORDER = HEX(0x5C3A1E);
static const Color BORDER_TEXT = HEX(0xE8D5B7);
static const Color ACCENT_LIGHT = HEX(0xE2C79F);
static const Color ACCENT_DARK = HEX(0xA37752);

static const Color SQUARE_SELECTED = HEXA(0xE07B2A, 110);
static const Color SQUARE_TARGET = HEXA(0xF2D04A, 110);
static const Color SQUARE_ATTACK = HEXA(0xCC3B2F, 150);

// --- Layout --------------------------------------------------------------------------------------

typedef struct {
  Rectangle board;
  Rectangle status;
  Rectangle chat;
} Layout;

static Rectangle cut_bottom(Rectangle *r, float h) {
  Rectangle out = { r->x, r->y + r->height - h, r->width, h };
  r->height -= h;
  return out;
}

static Layout compute_layout(int w, int h) {
  Rectangle rest = { 0, 0, w, h };
  Layout l;

  l.status = cut_bottom(&rest, STATUS_BAR_HEIGHT);

  float chat_w = w * 0.25f;
  if (chat_w < CHAT_MIN_WIDTH) {
    chat_w = CHAT_MIN_WIDTH;
  }

  float avail_w = rest.width - chat_w - (2 * BORDER_SIZE);
  float avail_h = rest.height - (2 * BORDER_SIZE);
  int sq = (int)(avail_w < avail_h ? avail_w : avail_h) / 8;
  if (sq < 1) {
    sq = 1;
  }
  float size = sq * 8;
  float outer = size + (2 * BORDER_SIZE);

  float total_w = outer + chat_w;
  float x = rest.x + ((rest.width - total_w) / 2);
  float y = rest.y + ((rest.height - outer) / 2);

  l.board = (Rectangle){ x + BORDER_SIZE, y + BORDER_SIZE, size, size };
  l.chat = (Rectangle){ x + outer, y, chat_w, outer };
  return l;
}

// --- Assets --------------------------------------------------------------------------------------

typedef struct {
  Image piece_images[PIECE_SLOTS];
  Texture2D pieces[PIECE_SLOTS];
  int piece_size;
  Font font;
} Assets;

static Assets assets;

static void load_assets(void) {
  static const char NAMES[] = " PNBRQK";
  const char *dir = GetApplicationDirectory();

  for (int type = PAWN; type <= KING; type++) {
    Piece w = type | COLOR_WHITE;
    Piece b = type | COLOR_BLACK;
    assets.piece_images[w] = LoadImage(TextFormat("%sassets/pieces/w%c.png", dir, NAMES[type]));
    assets.piece_images[b] = LoadImage(TextFormat("%sassets/pieces/b%c.png", dir, NAMES[type]));
  }

  assets.font =
      LoadFontEx(TextFormat("%sassets/fonts/Inter-Medium.ttf", dir), FONT_ATLAS_SIZE, NULL, 0);
  SetTextureFilter(assets.font.texture, TEXTURE_FILTER_BILINEAR);
}

static void rebuild_piece_textures(int sq) {
  if (sq == assets.piece_size || sq <= 0) {
    return;
  }

  for (int i = 0; i < PIECE_SLOTS; i++) {
    if (assets.piece_images[i].data == NULL) {
      continue;
    }
    if (assets.pieces[i].id != 0) {
      UnloadTexture(assets.pieces[i]);
    }

    Image img = ImageCopy(assets.piece_images[i]);
    ImageResize(&img, sq, sq);
    assets.pieces[i] = LoadTextureFromImage(img);
    UnloadImage(img);

    GenTextureMipmaps(&assets.pieces[i]);
    SetTextureFilter(assets.pieces[i], TEXTURE_FILTER_BILINEAR);
  }

  assets.piece_size = sq;
}

static void unload_assets(void) {
  UnloadFont(assets.font);

  for (int i = 0; i < PIECE_SLOTS; i++) {
    if (assets.pieces[i].id != 0) {
      UnloadTexture(assets.pieces[i]);
    }
    if (assets.piece_images[i].data != NULL) {
      UnloadImage(assets.piece_images[i]);
    }
  }
}

// --- Selection -----------------------------------------------------------------------------------

typedef struct {
  int selected;
  bool targets[64];
} Input;

static Input input = { .selected = -1 };

static void select_square(const Game *g, int sq) {
  input.selected = sq;
  generate_legal_destinations(g, sq, input.targets);
}

// --- Board geometry ------------------------------------------------------------------------------

typedef struct {
  int ox, oy, sq;
} BoardGeometry;

static BoardGeometry compute_board_geometry(Rectangle rect) {
  BoardGeometry bg;
  bg.sq = (int)(rect.width < rect.height ? rect.width : rect.height) / 8;
  bg.ox = (int)(rect.x + ((rect.width - bg.sq * 8) / 2));
  bg.oy = (int)(rect.y + ((rect.height - bg.sq * 8) / 2));
  return bg;
}

static int square_at(Vector2 m, bool is_white, Rectangle board) {
  BoardGeometry bg = compute_board_geometry(board);
  if (m.x < bg.ox || m.y < bg.oy) {
    return -1;
  }
  int col = (int)(m.x - bg.ox) / bg.sq;
  int row = (int)(m.y - bg.oy) / bg.sq;
  if (col > 7 || row > 7) {
    return -1;
  }
  int file = is_white ? col : 7 - col;
  int rank = is_white ? 7 - row : row;
  return (rank * 8) + file;
}

// --- Board drawing -------------------------------------------------------------------------------

static void draw_border(Rectangle board) {
  DrawRectangleRec(
      (Rectangle){
          board.x - BORDER_SIZE,
          board.y - BORDER_SIZE,
          board.width + (2 * BORDER_SIZE),
          board.height + (2 * BORDER_SIZE),
      },
      BOARD_BORDER);
}

static void draw_coordinates(Rectangle board, bool is_white) {
  BoardGeometry bg = compute_board_geometry(board);

  int font_size = BORDER_SIZE;
  float spacing = font_size / 10.0f;

  for (int i = 0; i < 8; i++) {
    char file_label[2] = { (char)('a' + (is_white ? i : 7 - i)), 0 };
    char rank_label[2] = { (char)('1' + (is_white ? 7 - i : i)), 0 };

    float file_w = MeasureTextEx(assets.font, file_label, font_size, spacing).x;
    float rank_w = MeasureTextEx(assets.font, rank_label, font_size, spacing).x;

    DrawTextEx(assets.font, file_label,
               (Vector2){ bg.ox + (i * bg.sq) + ((bg.sq - file_w) / 2),
                          bg.oy + (8 * bg.sq) + ((BORDER_SIZE - font_size) / 2.0f) },
               font_size, spacing, BORDER_TEXT);

    DrawTextEx(assets.font, rank_label,
               (Vector2){ bg.ox - BORDER_SIZE + ((BORDER_SIZE - rank_w) / 2),
                          bg.oy + (i * bg.sq) + ((bg.sq - font_size) / 2.0f) },
               font_size, spacing, BORDER_TEXT);
  }
}

static void draw_square_base(int x, int y, int size, bool light) {
  DrawRectangle(x, y, size, size, light ? ACCENT_LIGHT : ACCENT_DARK);
  int inner = size - (2 * ACCENT_SIZE);
  if (inner > 0) {
    DrawRectangle(x + ACCENT_SIZE, y + ACCENT_SIZE, inner, inner, light ? BOARD_LIGHT : BOARD_DARK);
  }
}

static Color square_highlight(const Game *g, const Input *in, int idx) {
  if (idx == in->selected) {
    return SQUARE_SELECTED;
  }
  if (!in->targets[idx]) {
    return BLANK;
  }
  bool pawn_selected = in->selected >= 0 && TYPE(g->board[in->selected]) == PAWN;
  bool capture = !IS_EMPTY(g->board[idx]) || (pawn_selected && idx == g->en_passant_square);
  return capture ? SQUARE_ATTACK : SQUARE_TARGET;
}

static void draw_piece(Piece p, int x, int y) {
  if (!IS_EMPTY(p)) {
    DrawTexture(assets.pieces[p], x, y, WHITE);
  }
}

static void draw_board(const Game *g, bool is_white, const Input *in, Rectangle rect) {
  BoardGeometry bg = compute_board_geometry(rect);

  for (int rank = 0; rank < 8; rank++) {
    for (int file = 0; file < 8; file++) {
      int col = is_white ? file : 7 - file;
      int row = is_white ? 7 - rank : rank;
      int x = bg.ox + (col * bg.sq);
      int y = bg.oy + (row * bg.sq);
      int idx = (rank * 8) + file;

      draw_square_base(x, y, bg.sq, (rank + file) % 2 != 0);
      DrawRectangle(x, y, bg.sq, bg.sq, square_highlight(g, in, idx));
      draw_piece(g->board[idx], x, y);
    }
  }
}

// --- Status bar ----------------------------------------------------------------------------------

static const char *reason_text(uint8_t reason) {
  switch (reason) {
  case REASON_CHECKMATE:
    return "by checkmate";
  case REASON_STALEMATE:
    return "by stalemate";
  case REASON_RESIGN:
    return "by resignation";
  case REASON_DISCONNECT:
    return "by disconnection";
  case REASON_FIFTY_MOVES:
    return "by the fifty-move rule";
  case REASON_INSUFFICIENT_MATERIAL:
    return "by insufficient material";
  default:
    return NULL;
  }
}

static const char *winner_text(uint8_t result) {
  switch (result) {
  case RESULT_WHITE_WINS:
    return "White wins";
  case RESULT_BLACK_WINS:
    return "Black wins";
  case RESULT_DRAW:
    return "Draw";
  default:
    return "Game over";
  }
}

static const char *result_text(const View *v) {
  const char *winner = winner_text(v->result.result);
  const char *reason = reason_text(v->result.reason);
  return reason != NULL ? TextFormat("%s %s", winner, reason) : winner;
}

// A code this build predates still has to show the user something.
static const char *error_text(const View *v) {
  const char *message = error_message(v->error_code);
  return message ? message : TextFormat("server error (%u)", v->error_code);
}

static const char *status_text(const View *v) {
  if (!v->connected) {
    return "Disconnected";
  }
  if (v->error_code != ERR_NONE) {
    return error_text(v);
  }
  if (!v->joined) {
    return "Connecting...";
  }
  if (v->game_over) {
    return result_text(v);
  }
  if (!v->has_state) {
    return TextFormat("Room %s - waiting for opponent", v->room_id);
  }
  if (v->spectator) {
    return v->game.turn == COLOR_WHITE ? "White to move" : "Black to move";
  }
  return v->game.turn == v->my_color ? "Your move" : "Opponent's move";
}

static void draw_status(const View *v, Rectangle r) {
  DrawRectangleRec(r, (Color){ 30, 30, 30, 255 });
  DrawTextEx(assets.font, status_text(v),
             (Vector2){ r.x + 10, r.y + ((r.height - STATUS_FONT_SIZE) / 2.0f) }, STATUS_FONT_SIZE,
             STATUS_FONT_SIZE / 10.0f, RAYWHITE);
}

// --- Frame ---------------------------------------------------------------------------------------

static void draw_chat(Rectangle r) {
  DrawRectangleRec(r, (Color){ 10, 10, 10, 255 });
}

static void handle_input(ClientState *cs, const View *v, Rectangle board) {
  bool my_turn = v->has_state && !v->game_over && !v->spectator && v->game.turn == v->my_color;
  if (!my_turn) {
    select_square(&v->game, -1);
    return;
  }

  if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_ESCAPE)) {
    select_square(&v->game, -1);
    return;
  }

  if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
    return;
  }

  int sq = square_at(GetMousePosition(), v->my_color == COLOR_WHITE, board);
  if (sq < 0) {
    return;
  }

  if (IS_COLOR(v->game.board[sq], v->my_color)) {
    select_square(&v->game, sq);
  } else if (input.selected >= 0 && input.targets[sq]) {
    pthread_mutex_lock(&cs->lock);
    cs->pending_move = (Move){ input.selected, sq, EMPTY };
    cs->has_pending_move = true;
    pthread_mutex_unlock(&cs->lock);
    select_square(&v->game, -1);
  }
}

static void render(View *v, Layout *l) {
  // int w = GetScreenWidth();
  // int h = GetScreenHeight();

  rebuild_piece_textures((int)l->board.width / 8);

  BeginDrawing();
  ClearBackground((Color){ 40, 40, 40, 255 });
  draw_border(l->board);
  draw_coordinates(l->board, v->my_color == COLOR_WHITE);
  draw_board(&v->game, v->my_color == COLOR_WHITE, &input, l->board);
  draw_chat(l->chat);
  draw_status(v, l->status);
  EndDrawing();
}

void run_ui(ClientState *cs) {
  SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI | FLAG_MSAA_4X_HINT);
  SetTraceLogLevel(LOG_WARNING);
  InitWindow(800, 800, "CChess");
  SetTargetFPS(60);
  SetExitKey(KEY_NULL);

  load_assets();

  while (!WindowShouldClose()) {
    pthread_mutex_lock(&cs->lock);
    View snapshot = cs->view;
    pthread_mutex_unlock(&cs->lock);

    Layout l = compute_layout(GetScreenWidth(), GetScreenHeight());
    handle_input(cs, &snapshot, l.board);
    render(&snapshot, &l);
  }

  unload_assets();
  CloseWindow();
}
