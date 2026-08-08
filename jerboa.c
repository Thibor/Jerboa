#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#endif

/* define the mate value */
#define MATE 10000

#define WHITE 0
#define BLACK 8
#define MAX_PLY 64
#define KEY_SIZE 1024
#define TT_SIZE (64ULL << 15)
#define U8 unsigned __int8
#define S16 signed __int16
#define S64 signed __int64
#define U64 unsigned __int64
#define FALSE 0
#define TRUE 1
#define NAME "Jerboa"
#define VERSION "2026-07-30"
#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

/* define the piece type: empty, pawn, knight, bishop, rook, queen, king */
typedef enum TPieceType { EMPTY, PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, PT_NB } TPieceType;
typedef enum Bound { UPPER, LOWER, EXACT } Bound;
enum {CASTLE_WK = 0b0001, CASTLE_WQ = 0b0010, CASTLE_BK = 0b0100, CASTLE_BQ = 0b1000};
/* define the move type, for example
   KING|CASTLE is a castle move
   PAWN|CAPTURE|EP is an enpassant move
   PAWN|PROMO|CAPTURE is a promotion with a capture */
typedef enum { CASTLE = 0x40, PROMO = 0x20, EP = 0x10, CAPTURE = 0x08 } TMoveType;

/* bitboard types */
typedef uint64_t TBB;

typedef struct {
	U8 post;
	U8 stop;
	U8 depthLimit;
	U64 timeStart;
	U64 timeLimit;
	U64 nodes;
	U64 nodesLimit;
}SearchInfo;

/* move structure */
typedef union
{
	struct {
		uint8_t MoveType;
		uint8_t From;
		uint8_t To;
		uint8_t Prom;
	};
	unsigned int Move;
}TMove;

/*
Board structure definition

PM,P0,P1,P2 are the 4 bitboards that contain the whole board
PM is the bitboard with the side to move pieces
P0,P1 and P2: with these bitboards you can obtain every type of pieces and every pieces combinations.
*/
typedef struct{
	TBB PM;
	TBB P0;
	TBB P1;
	TBB P2;
	uint8_t castleFlags; /* ..sl..SL  short long opponent SHORT LONG side to move */
	uint8_t enPassant; /* enpassant column, =8 if not set */
	uint8_t move50; /* 50 move rule counter */
	uint8_t STM; /* side to move */
} TBoard;

typedef struct {
	TMove move;
	TMove killer1;
	TMove killer2;
} Stack;

typedef struct {
	U64 hash;
	TMove move;
	S16 score;
	U8 depth;
	U8 flag;
}TTEntry;

/*
Into Game are saved all the positions from the last 50 move counter reset
Position is the pointer to the last position of the game
*/
TBoard Game[512];
TBoard* position;

U64 keys[KEY_SIZE];
const int StaticValue[8] = { 0,100,300,300,500,950,0,0 };
Stack ss[128];
int hh[2][64][64];
TTEntry tt[TT_SIZE];
int historyCount = 0;
U64 historyHash[1024];

/* Piece Square Tables */
const int PST[8][64] =
{
{  0,  0,  0,  0,  0,  0,  0,  0, /* empty */
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0,
   0,  0,  0,  0,  0,  0,  0,  0},
{  0,  0,  0,  0,  0,  0,  0,  0, /* pawn */
   5,  5,  5,-20,-20,  5,  5,  5,
   5, -5, -5,  0,  0, -5, -5,  5,
   0,  0,  0, 12, 12,  0,  0,  0,
  10, 10, 10, 16, 16, 10, 10, 10,
  30, 30, 30, 40, 40, 30, 30, 30,
 100,100,100,100,100,100,100,100,
   0,  0,  0,  0,  0,  0,  0,  0},
{ -5, -5,  0,  0,  0,  0, -5, -5,  /* knight */
  -5,  0, 10, 10, 10, 10,  0, -5,
   0, 10, 15, 15, 15, 15, 10,  0,
   0, 10, 15, 20, 20, 15, 10,  0,
   0, 10, 15, 20, 20, 15, 10,  0,
   0, 10, 15, 15, 15, 15, 10,  0,
  -5,  0, 10, 10, 10, 10,  0, -5,
  -5, -5,  0,  0,  0,  0, -5, -5},
{ -5, -5,  0,  0,  0,  0, -5, -5,  /* bishop */
  -5,  0, 10, 10, 10, 10,  0, -5,
  -5, 10, 15, 15, 15, 15, 10, -5,
  -5, 10, 15, 20, 20, 15, 10, -5,
  -5, 10, 15, 20, 20, 15, 10, -5,
  -5, 10, 15, 15, 15, 15, 10, -5,
  -5,  0, 10, 10, 10, 10,  0, -5,
  -5, -5,  0,  0,  0,  0, -5, -5},
{  0, -5, -5, 10, 10, -5, -5,  0, /* rook */
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
   0,  0,  0, 10, 10,  0,  0,  0,
  20, 20, 20, 20, 20, 20, 20, 20,
  10, 10, 10, 10, 10, 10, 10, 10},
{ -4, -4, -4, -4, -4, .4, -4, -4,  /* queen */
  -4,  0,  0,  0,  0,  0,  0, -4,
  -4,  0,  4,  4,  4,  4,  0, -4,
  -4,  0,  4,  8,  8,  4,  0, -4,
  -4,  0,  4,  8,  8,  4,  0, -4,
  -4,  0,  4,  4,  4,  4,  0, -4,
  -4,  0,  0,  0,  0,  0,  0, -4,
  -4, -4, -4, -4, -4, -4, -4, -4},
{ -5, 25, 15,-10,  0,-10, 30, -5, /* king middlegame */
  -5, -5, -5, -5, -5, -5, -5, -5,
  -5, -5, -5, -5, -5, -5, -5, -5,
  -5, -5, -5, -5, -5, -5, -5, -5,
  -5, -5, -5, -5, -5, -5, -5, -5,
  -5, -5, -5, -5, -5, -5, -5, -5,
  -5, -5, -5, -5, -5, -5, -5, -5,
  -5, -5, -5, -5, -5, -5, -5, -5},
{  0, -5, -5,  0,  0,  0, -5,  0, /* king endgame */
   0,  5,  5,  5,  5,  5,  5,  0,
   0,  5, 10, 10, 10, 10,  5,  0,
   0,  5, 10, 15, 15, 10,  5,  0,
   0,  5, 10, 15, 15, 10,  5,  0,
   0,  5, 10, 10, 10, 10,  5,  0,
   0,  5,  5,  5,  5,  5,  5,  0,
   0,  0,  0,  0,  0,  0,  0,  0}
};

/* array of bitboards that contains all the knight destination for every square */
const TBB KnightDest[64] = { 0x0000000000020400ULL,0x0000000000050800ULL,0x00000000000a1100ULL,0x0000000000142200ULL,
						   0x0000000000284400ULL,0x0000000000508800ULL,0x0000000000a01000ULL,0x0000000000402000ULL,
						   0x0000000002040004ULL,0x0000000005080008ULL,0x000000000a110011ULL,0x0000000014220022ULL,
						   0x0000000028440044ULL,0x0000000050880088ULL,0x00000000a0100010ULL,0x0000000040200020ULL,
						   0x0000000204000402ULL,0x0000000508000805ULL,0x0000000a1100110aULL,0x0000001422002214ULL,
						   0x0000002844004428ULL,0x0000005088008850ULL,0x000000a0100010a0ULL,0x0000004020002040ULL,
						   0x0000020400040200ULL,0x0000050800080500ULL,0x00000a1100110a00ULL,0x0000142200221400ULL,
						   0x0000284400442800ULL,0x0000508800885000ULL,0x0000a0100010a000ULL,0x0000402000204000ULL,
						   0x0002040004020000ULL,0x0005080008050000ULL,0x000a1100110a0000ULL,0x0014220022140000ULL,
						   0x0028440044280000ULL,0x0050880088500000ULL,0x00a0100010a00000ULL,0x0040200020400000ULL,
						   0x0204000402000000ULL,0x0508000805000000ULL,0x0a1100110a000000ULL,0x1422002214000000ULL,
						   0x2844004428000000ULL,0x5088008850000000ULL,0xa0100010a0000000ULL,0x4020002040000000ULL,
						   0x0400040200000000ULL,0x0800080500000000ULL,0x1100110a00000000ULL,0x2200221400000000ULL,
						   0x4400442800000000ULL,0x8800885000000000ULL,0x100010a000000000ULL,0x2000204000000000ULL,
						   0x0004020000000000ULL,0x0008050000000000ULL,0x00110a0000000000ULL,0x0022140000000000ULL,
						   0x0044280000000000ULL,0x0088500000000000ULL,0x0010a00000000000ULL,0x0020400000000000ULL };
/* The same for the king */
const TBB KingDest[64] = { 0x0000000000000302ULL,0x0000000000000705ULL,0x0000000000000e0aULL,0x0000000000001c14ULL,
						  0x0000000000003828ULL,0x0000000000007050ULL,0x000000000000e0a0ULL,0x000000000000c040ULL,
						  0x0000000000030203ULL,0x0000000000070507ULL,0x00000000000e0a0eULL,0x00000000001c141cULL,
						  0x0000000000382838ULL,0x0000000000705070ULL,0x0000000000e0a0e0ULL,0x0000000000c040c0ULL,
						  0x0000000003020300ULL,0x0000000007050700ULL,0x000000000e0a0e00ULL,0x000000001c141c00ULL,
						  0x0000000038283800ULL,0x0000000070507000ULL,0x00000000e0a0e000ULL,0x00000000c040c000ULL,
						  0x0000000302030000ULL,0x0000000705070000ULL,0x0000000e0a0e0000ULL,0x0000001c141c0000ULL,
						  0x0000003828380000ULL,0x0000007050700000ULL,0x000000e0a0e00000ULL,0x000000c040c00000ULL,
						  0x0000030203000000ULL,0x0000070507000000ULL,0x00000e0a0e000000ULL,0x00001c141c000000ULL,
						  0x0000382838000000ULL,0x0000705070000000ULL,0x0000e0a0e0000000ULL,0x0000c040c0000000ULL,
						  0x0003020300000000ULL,0x0007050700000000ULL,0x000e0a0e00000000ULL,0x001c141c00000000ULL,
						  0x0038283800000000ULL,0x0070507000000000ULL,0x00e0a0e000000000ULL,0x00c040c000000000ULL,
						  0x0302030000000000ULL,0x0705070000000000ULL,0x0e0a0e0000000000ULL,0x1c141c0000000000ULL,
						  0x3828380000000000ULL,0x7050700000000000ULL,0xe0a0e00000000000ULL,0xc040c00000000000ULL,
						  0x0203000000000000ULL,0x0507000000000000ULL,0x0a0e000000000000ULL,0x141c000000000000ULL,
						  0x2838000000000000ULL,0x5070000000000000ULL,0xa0e0000000000000ULL,0x40c0000000000000ULL };

/* masks for finding the pawns that can capture with an enpassant (in move generation) */
const TBB enPassant[8] = {
0x0000000200000000ULL,0x0000000500000000ULL,0x0000000A00000000ULL,0x0000001400000000ULL,
0x0000002800000000ULL,0x0000005000000000ULL,0x000000A000000000ULL,0x0000004000000000ULL
};

/* masks for finding the pawns that can capture with an enpassant (in make move) */
const TBB EnPassantM[8] = {
0x0000000002000000ULL,0x0000000005000000ULL,0x000000000A000000ULL,0x0000000014000000ULL,
0x0000000028000000ULL,0x0000000050000000ULL,0x00000000A0000000ULL,0x0000000040000000ULL
};

/*
reverse a bitboard:
A bitboard is an array of byte: Byte0,Byte1,Byte2,Byte3,Byte4,Byte5,Byte6,Byte7
after this function the bitboard will be: Byte7,Byte6,Byte5,Byte4,Byte3,Byte2,Byte1,Byte0

The board is saved always with the side to move in the low significant bits of the bitboard, so this function
is used to change the side to move
*/

SearchInfo info;

#if defined(_MSC_VER)

#define RevBB(bb) (_byteswap_uint64(bb))

unsigned long __inline MSB(unsigned __int64 value)
{
	unsigned long leading_zero = 0;

	if (_BitScanReverse64(&leading_zero, value))
	{
		return 0x3F ^ (63 - leading_zero);
	}
	else
	{
		return 0x3F ^ 64;
	}
}

unsigned long __inline LSB(unsigned __int64 value)
{
	unsigned long trailing_zero = 0;

	if (_BitScanForward64(&trailing_zero, value))
	{
		return trailing_zero;
	}
	else
	{
		return 64;
	}
}

#define PopCount(bb) (__popcnt64(bb))

#else
#define RevBB(bb) (__builtin_bswap64(bb))
/* return the index of the most significant bit of the bitboard, bb must always be !=0 */
#define MSB(bb) (0x3F ^ __builtin_clzll(bb))
/* return the index of the least significant bit of the bitboard, bb must always be !=0 */
#define LSB(bb) (__builtin_ctzll(bb))
/* return the number of bits sets of a bitboard */
#define PopCount(bb) (__builtin_popcountll(bb))
#endif


/* extract the least significant bit of the bitboard */
#define ExtractLSB(bb) ((bb)&(-(bb)))
/* reset the least significant bit of bb */
#define ClearLSB(bb) ((bb)&((bb)-1))

/* Macro to check and reset the castle rights:
   CastleSM: short castling side to move
   CastleLM: long castling side to move
   CastleSO: short castling opponent
   CastleLO: long castling opponent
 */
#define CastleMK (position->castleFlags & CASTLE_WK)
#define CastleMQ (position->castleFlags & CASTLE_WQ)
#define CastleEK (position->castleFlags & CASTLE_BK)
#define CastleEQ (position->castleFlags & CASTLE_BQ)
#define ResetCastleMK (position->castleFlags &= ~CASTLE_WK)
#define ResetCastleMQ (position->castleFlags &= ~CASTLE_WQ)
#define ResetCastleEK (position->castleFlags &= ~CASTLE_BK)
#define ResetCastleEQ (position->castleFlags &= ~CASTLE_BQ)

 /* these Macros are used to calculate the bitboard of a particular kind of piece

	P2 P1 P0
	 0  0  0    empty
	 0  0  1    pawn
	 0  1  0    knight
	 0  1  1    bishop
	 1  0  0    rook
	 1  0  1    queen
	 1  1  0    king
 */
#define Occupation (position->P0 | position->P1 | position->P2) /* board occupation */
#define Pawns (position->P0 & ~position->P1 & ~position->P2) /* all the pawns on the board */
#define Knights (~position->P0 & position->P1 & ~position->P2)
#define Bishops (position->P0 & position->P1)
#define Rooks (~position->P0 & ~position->P1 & position->P2)
#define Queens (position->P0 & position->P2)
#define Kings (position->P1 & position->P2) /* a bitboard with the 2 kings */

 /* get the piece type giving the square */
#define Piece(sq) (((position->PM>>(sq))&1)<<3 | ((position->P2>>(sq))&1)<<2 | ((position->P1>>(sq))&1)<<1 | ((position->P0>>(sq))&1))
#define PieceType(sq) (((position->P2>>(sq))&1)<<2 | ((position->P1>>(sq))&1)<<1 | ((position->P0>>(sq))&1))

/* calculate the square related to the opponent */
#define OppSq(sp) ((sp)^0x38)
/* Absolute Square, we need this macro to return the move in long algebric notation  */
#define AbsSq(sq,col) ((col)==WHITE ? (sq):OppSq(sq))

/*
The board is always saved with the side to move in the lower part of the bitboards to use the same generation and
make for the Black and the White side.
This needs the inversion of the 4 bitboards, roll the Castle rights and update the side to move.
*/
#define ChangeSide \
do{ \
   position->PM^=Occupation; /* update the side to move pieces */\
   position->PM=RevBB(position->PM);\
   position->P0=RevBB(position->P0);\
   position->P1=RevBB(position->P1);\
   position->P2=RevBB(position->P2);/* reverse the board */\
   position->castleFlags = (position->castleFlags>>2)|((position->castleFlags<<2)&0b1100);/* roll the castle rights */\
   position->STM ^= BLACK; /* change the side to move */\
}while(0)

static inline void TTClear() { memset(tt, 0, sizeof(tt)); }
static inline void HHClear() { memset(hh, 0, sizeof(hh)); }
static inline void SSClear() { memset(ss, 0, sizeof(ss)); }
static inline U64 GetTimeMs() { return GetTickCount64(); }

void UciCommand(char* str);

static int InputAvailable(void) {
	static int init = 0, pipe;
	static HANDLE inh;
	DWORD dw;
	if (!init) {
		init = 1;
		inh = GetStdHandle(STD_INPUT_HANDLE);
		pipe = !GetConsoleMode(inh, &dw);
		if (!pipe) {
			SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
			FlushConsoleInputBuffer(inh);
		}
	}
	if (pipe) {
		if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL))
			return 1;
		return dw > 0;
	}
	else {
		GetNumberOfConsoleInputEvents(inh, &dw);
		return dw > 1;
	}
}

static int CheckUp() {
	if ((++info.nodes & 0xffff) == 0) {
		if (info.timeLimit && GetTimeMs() - info.timeStart > info.timeLimit)
			info.stop = TRUE;
		if (info.nodesLimit && info.nodes > info.nodesLimit)
			info.stop = TRUE;
		if (InputAvailable()) {
			char str[4000];
			fgets(str, sizeof(str), stdin);
			UciCommand(str);
		}
	}
	return info.stop;
}

/* get the corresponding string to the given move  */
static inline void MoveToStr(char* strmove, TMove move, uint8_t tomove)
{
	const char promo[7] = "\0\0nbrq";
	strmove[0] = 'a' + AbsSq(move.From, tomove) % 8;
	strmove[1] = '1' + AbsSq(move.From, tomove) / 8;
	strmove[2] = 'a' + AbsSq(move.To, tomove) % 8;
	strmove[3] = '1' + AbsSq(move.To, tomove) / 8;
	strmove[4] = promo[move.Prom];
	strmove[5] = '\0';
}

/* get the corresponding move to the given string */
static inline TMove StrToMove(char* strmove)
{
	TMove move;
	move.From = AbsSq((uint8_t)(strmove[0] - 'a' + (strmove[1] - '1') * 8), position->STM);
	move.To = AbsSq((uint8_t)(strmove[2] - 'a' + (strmove[3] - '1') * 8), position->STM);
	move.Prom = EMPTY;
	if (strmove[4] == 'n') move.Prom = KNIGHT;
	else if (strmove[4] == 'b') move.Prom = BISHOP;
	else if (strmove[4] == 'r') move.Prom = ROOK;
	else if (strmove[4] == 'q') move.Prom = QUEEN;
	move.MoveType = PieceType(move.From);
	if (move.MoveType == PAWN) {
		if ((1ULL << move.To) & 0xFF00000000000000ULL)
			move.MoveType |= PROMO;
		else if (position->enPassant != 8 && move.To == (40 + position->enPassant))
			move.MoveType = PAWN | EP | CAPTURE;
	}
	else if (move.MoveType == KING && (move.To - move.From == 2 || move.From - move.To == 2))
		move.MoveType = KING | CASTLE;
	if (PieceType(move.To))
		move.MoveType |= CAPTURE;
	return move;
}

static U64 Rand64() {
	static U64 next = 1;
	next = next * 12345104729 + 104723;
	return next;
}

static void InitHash() {
	for (int i = 0; i < KEY_SIZE; ++i)
		keys[i] = Rand64();
}

/* return the bitboard with the rook destinations */
static inline TBB GenRook(uint64_t sq, TBB occupation)
{
	TBB piece = 1ULL << sq;
	occupation ^= piece; /* remove the selected piece from the occupation */
	TBB piecesup = (0x0101010101010101ULL << sq) & (occupation | 0xFF00000000000000ULL); /* find the pieces up */
	TBB piecesdo = (0x8080808080808080ULL >> (63 - sq)) & (occupation | 0x00000000000000FFULL); /* find the pieces down */
	TBB piecesri = (0x00000000000000FFULL << sq) & (occupation | 0x8080808080808080ULL); /* find pieces on the right */
	TBB piecesle = (0xFF00000000000000ULL >> (63 - sq)) & (occupation | 0x0101010101010101ULL); /* find pieces on the left */
	return (((0x8080808080808080ULL >> (63 - LSB(piecesup))) & (0x0101010101010101ULL << MSB(piecesdo))) |
		((0xFF00000000000000ULL >> (63 - LSB(piecesri))) & (0x00000000000000FFULL << MSB(piecesle)))) ^ piece;
	/* From every direction find the first piece and from that piece put a mask in the opposite direction.
	   Put togheter all the 4 masks and remove the moving piece */
}

/* return the bitboard with the bishops destinations */
static inline TBB GenBishop(uint64_t sq, TBB occupation)
{  /* it's the same as the rook */
	TBB piece = 1ULL << sq;
	occupation ^= piece;
	TBB piecesup = (0x8040201008040201ULL << sq) & (occupation | 0xFF80808080808080ULL);
	TBB piecesdo = (0x8040201008040201ULL >> (63 - sq)) & (occupation | 0x01010101010101FFULL);
	TBB piecesle = (0x8102040810204081ULL << sq) & (occupation | 0xFF01010101010101ULL);
	TBB piecesri = (0x8102040810204081ULL >> (63 - sq)) & (occupation | 0x80808080808080FFULL);
	return (((0x8040201008040201ULL >> (63 - LSB(piecesup))) & (0x8040201008040201ULL << MSB(piecesdo))) |
		((0x8102040810204081ULL >> (63 - LSB(piecesle))) & (0x8102040810204081ULL << MSB(piecesri)))) ^ piece;
}

/* return the bitboard with pieces of the same type */
static inline TBB BBPieces(TPieceType piece)
{
	switch (piece) // find the bb with the pieces of the same type
	{
	case PAWN: return Pawns;
	case KNIGHT: return Knights;
	case BISHOP: return Bishops;
	case ROOK: return Rooks;
	case QUEEN: return Queens;
	case KING: return Kings;
	}
}

/* return the bitboard with the destinations of a piece in a square (exept for pawns) */
static inline TBB BBDestinations(TPieceType piece, uint64_t sq, TBB occupation)
{
	switch (piece) // generate the destination squares of the piece
	{
	case KNIGHT: return KnightDest[sq];
	case BISHOP: return GenBishop(sq, occupation);
	case ROOK: return GenRook(sq, occupation);
	case QUEEN: return GenRook(sq, occupation) | GenBishop(sq, occupation);
	case KING: return KingDest[sq];
	}
}

/* If the king is in check this function return the pieces that are attacking the king. If there aren't it returns 0 */
static inline TBB InCheck(void)
{
	TBB occupation, opposing;
	TBB king = Kings & position->PM;
	uint64_t kingsq = LSB(king);
	occupation = Occupation;
	opposing = position->PM ^ occupation;
	return (((KnightDest[kingsq] & Knights) |
		(GenRook(kingsq, occupation) & (Rooks | Queens)) |
		(GenBishop(kingsq, occupation) & (Bishops | Queens)) |
		((((king << 9) & 0xFEFEFEFEFEFEFEFEULL) | ((king << 7) & 0x7F7F7F7F7F7F7F7FULL)) & Pawns) |
		(KingDest[kingsq] & Kings)) & opposing);
}

/* try the move and see if the king is in check. If so return the attacking pieces, if not return 0 */
static inline TBB Illegal(TMove move)
{
	TBB From, To;
	From = 1ULL << move.From;
	To = 1ULL << move.To;
	TBB occupation, opposing;
	occupation = Occupation;
	opposing = position->PM ^ occupation;
	TBB newoccupation, newopposing;
	TBB king;
	uint64_t kingsq;
	newoccupation = (occupation ^ From) | To;
	newopposing = opposing & ~To;
	if ((move.MoveType & 0x07) == KING)
	{
		king = To;
		kingsq = move.To;
	}
	else
	{
		king = Kings & position->PM;
		kingsq = LSB(king);
		if (move.MoveType & EP) { newopposing ^= To >> 8; newoccupation ^= To >> 8; }
	}
	return (((KnightDest[kingsq] & Knights) |
		(GenRook(kingsq, newoccupation) & (Rooks | Queens)) |
		(GenBishop(kingsq, newoccupation) & (Bishops | Queens)) |
		((((king << 9) & 0xFEFEFEFEFEFEFEFEULL) | ((king << 7) & 0x7F7F7F7F7F7F7F7FULL)) & Pawns) |
		(KingDest[kingsq] & Kings)) & newopposing);
}

/* Generate all pseudo-legal quiet moves */
static inline int GenerateQuiets(TMove* const quiets)
{
	TBB occupation, opposing;
	occupation = Occupation;
	opposing = occupation ^ position->PM;

	TMove* pquiets = quiets;
	for (TPieceType piece = KING; piece >= KNIGHT; piece--) // generate moves from king to knight
	{
		// generate moves for every piece of the same type of the side to move
		for (TBB pieces = BBPieces(piece) & position->PM; pieces; pieces = ClearLSB(pieces))
		{
			uint64_t sq = LSB(pieces);
			// for every destinations on a free square generate a move
			for (TBB destinations = ~occupation & BBDestinations(piece, sq, occupation); destinations; destinations = ClearLSB(destinations))
			{
				pquiets->MoveType = piece;
				pquiets->From = sq;
				pquiets->To = LSB(destinations);
				pquiets->Prom = EMPTY;
				pquiets++;
			}
		}
	}

	/* one pawns push */
	TBB push1 = (((Pawns & position->PM) << 8) & ~occupation) & 0x00FFFFFFFFFFFFFFULL;
	for (TBB pieces = push1; pieces; pieces = ClearLSB(pieces))
	{
		pquiets->MoveType = PAWN;
		pquiets->From = LSB(pieces) - 8;
		pquiets->To = LSB(pieces);
		pquiets->Prom = EMPTY;
		pquiets++;
	}

	/* double pawns pushes */
	for (TBB push2 = (push1 << 8) & ~occupation & 0x00000000FF000000ULL; push2; push2 = ClearLSB(push2))
	{
		pquiets->MoveType = PAWN;
		pquiets->From = LSB(push2) - 16;
		pquiets->To = LSB(push2);
		pquiets->Prom = EMPTY;
		pquiets++;
	}

	/* check if long castling is possible */
	if (CastleMQ && !(occupation & 0x0EULL))
	{
		TBB roo, bis;
		roo = ExtractLSB(0x1010101010101000ULL & occupation); /* column e */
		roo |= ExtractLSB(0x0808080808080800ULL & occupation); /*column d */
		roo |= ExtractLSB(0x0404040404040400ULL & occupation); /*column c */
		roo |= ExtractLSB(0x00000000000000E0ULL & occupation);  /* row 1 */
		bis = ExtractLSB(0x0000000102040800ULL & occupation); /*antidiag from e1/e8 */
		bis |= ExtractLSB(0x0000000001020400ULL & occupation); /*antidiag from d1/d8 */
		bis |= ExtractLSB(0x0000000000010200ULL & occupation); /*antidiag from c1/c8 */
		bis |= ExtractLSB(0x0000000080402000ULL & occupation); /*diag from e1/e8 */
		bis |= ExtractLSB(0x0000008040201000ULL & occupation); /*diag from d1/d8 */
		bis |= ExtractLSB(0x0000804020100800ULL & occupation); /*diag from c1/c8 */
		if (!(((roo & (Rooks | Queens)) | (bis & (Bishops | Queens)) | (0x00000000003E7700ULL & Knights) |
			(0x0000000000003E00ULL & Pawns) | (Kings & 0x0000000000000600ULL)) & opposing))
		{  /* check if c1/c8 d1/d8 e1/e8 are not attacked */
			pquiets->MoveType = KING | CASTLE;
			pquiets->From = 4;
			pquiets->To = 2;
			pquiets->Prom = EMPTY;
			pquiets++;
		}
	}
	/* check if short castling is possible */
	if (CastleMK && !(occupation & 0x60ULL))
	{
		TBB roo, bis;
		roo = ExtractLSB(0x1010101010101000ULL & occupation); /* column e */
		roo |= ExtractLSB(0x2020202020202000ULL & occupation); /* column f */
		roo |= ExtractLSB(0x4040404040404000ULL & occupation); /* column g */
		roo |= 1ULL << MSB(0x000000000000000FULL & (occupation | 0x1ULL));/* row 1 */
		bis = ExtractLSB(0x0000000102040800ULL & occupation); /* antidiag from e1/e8 */
		bis |= ExtractLSB(0x0000010204081000ULL & occupation); /*antidiag from f1/f8 */
		bis |= ExtractLSB(0x0001020408102000ULL & occupation); /*antidiag from g1/g8 */
		bis |= ExtractLSB(0x0000000080402000ULL & occupation); /*diag from e1/e8 */
		bis |= ExtractLSB(0x0000000000804000ULL & occupation); /*diag from f1/f8 */
		bis |= 0x0000000000008000ULL; /*diag from g1/g8 */
		if (!(((roo & (Rooks | Queens)) | (bis & (Bishops | Queens)) | (0x0000000000F8DC00ULL & Knights) |
			(0x000000000000F800ULL & Pawns) | (Kings & 0x0000000000004000ULL)) & opposing))
		{  /* check if e1/e8 f1/f8 g1/g8 are not attacked */
			pquiets->MoveType = KING | CASTLE;
			pquiets->From = 4;
			pquiets->To = 6;
			pquiets->Prom = EMPTY;
			pquiets++;
		}
	}
	return pquiets - quiets;
}

/* Generate all pseudo-legal capture and promotions */
static inline int GenerateCapture(TMove* const capture)
{
	TBB opposing, occupation;
	occupation = Occupation;
	opposing = position->PM ^ occupation;

	TMove* pcapture = capture;
	for (TPieceType piece = KING; piece >= KNIGHT; piece--) // generate moves from king to knight
	{
		// generate moves for every piece of the same type of the side to move
		for (TBB pieces = BBPieces(piece) & position->PM; pieces; pieces = ClearLSB(pieces))
		{
			uint64_t sq = LSB(pieces);
			// for every destinations on an opponent pieces generate a move
			for (TBB destinations = opposing & BBDestinations(piece, sq, occupation); destinations; destinations = ClearLSB(destinations))
			{
				pcapture->MoveType = piece | CAPTURE;
				pcapture->From = sq;
				pcapture->To = LSB(destinations);
				pcapture->Prom = EMPTY;
				pcapture++;
			}
		}
	}

	/* Generate pawns right captures */
	TBB pieces = Pawns & position->PM;
	for (TBB captureri = (pieces << 9) & 0x00FEFEFEFEFEFEFEULL & opposing; captureri; captureri = ClearLSB(captureri))
	{
		pcapture->MoveType = PAWN | CAPTURE;
		pcapture->From = LSB(captureri) - 9;
		pcapture->To = LSB(captureri);
		pcapture->Prom = EMPTY;
		pcapture++;
	}
	/* Generate pawns left captures */
	for (TBB capturele = (pieces << 7) & 0x007F7F7F7F7F7F7FULL & opposing; capturele; capturele = ClearLSB(capturele))
	{
		pcapture->MoveType = PAWN | CAPTURE;
		pcapture->From = LSB(capturele) - 7;
		pcapture->To = LSB(capturele);
		pcapture->Prom = EMPTY;
		pcapture++;
	}

	/* Generate pawns promotions */
	if (pieces & 0x00FF000000000000ULL)
	{
		/* promotions with left capture */
		for (TBB promo = (pieces << 9) & 0xFE00000000000000ULL & opposing; promo; promo = ClearLSB(promo)) {
			for (TPieceType piece = QUEEN; piece >= KNIGHT; piece--) /* generate underpromotions */
			{
				pcapture->MoveType = PAWN | PROMO | CAPTURE;
				pcapture->From = LSB(promo) - 9;
				pcapture->To = LSB(promo);
				pcapture->Prom = piece;
				pcapture++;
			}
		}
		/* promotions with right capture */
		for (TBB promo = (pieces << 7) & 0x7F00000000000000ULL & opposing; promo; promo = ClearLSB(promo))
		{
			for (TPieceType piece = QUEEN; piece >= KNIGHT; piece--) /* generate underpromotions */
			{
				pcapture->MoveType = PAWN | PROMO | CAPTURE;
				pcapture->From = LSB(promo) - 7;
				pcapture->To = LSB(promo);
				pcapture->Prom = piece;
				pcapture++;
			}
		}
		/* no capture promotions */
		for (TBB promo = ((pieces << 8) & ~occupation) & 0xFF00000000000000ULL; promo; promo = ClearLSB(promo))
		{
			for (TPieceType piece = QUEEN; piece >= KNIGHT; piece--) /* generate underpromotions */
			{
				pcapture->MoveType = PAWN | PROMO;
				pcapture->From = LSB(promo) - 8;
				pcapture->To = LSB(promo);
				pcapture->Prom = piece;
				pcapture++;
			}
		}
	}

	if (position->enPassant != 8)
	{  /* Generate EnPassant captures */
		for (TBB enpassant = pieces & enPassant[position->enPassant]; enpassant; enpassant = ClearLSB(enpassant))
		{
			pcapture->MoveType = PAWN | EP | CAPTURE;
			pcapture->From = LSB(enpassant);
			pcapture->To = 40 + position->enPassant;
			pcapture->Prom = EMPTY;
			pcapture++;
		}
	}
	return pcapture - capture;
}

static int GenerateMoves(TMove* const moves, int onlyCaptures) {
	int count = GenerateCapture(moves);
	if (!onlyCaptures)
		count += GenerateQuiets(moves + count);
	return count;
}

/* Make the move */
static inline void Make(TMove move)
{
	position++;
	*position = *(position - 1); /* copy the previous position into the last one */
	TBB part = 1ULL << move.From;
	TBB dest = 1ULL << move.To;
	switch (move.MoveType & 0x07)
	{
	case PAWN:
		if (move.MoveType & EP)
		{  /* EnPassant */
			position->PM ^= part | dest;
			position->P0 ^= part | dest;
			position->P0 ^= dest >> 8; /* delete the captured pawn */
			position->enPassant = 8;
		}
		else
		{
			if (move.MoveType & CAPTURE)
			{  /* Delete the captured piece */
				position->P0 &= ~dest;
				position->P1 &= ~dest;
				position->P2 &= ~dest;
			}
			if (move.MoveType & PROMO)
			{
				position->PM ^= part | dest;
				position->P0 ^= part;
				position->P0 |= (TBB)(move.Prom & 1) << (move.To);
				position->P1 |= (TBB)(((move.Prom) >> 1) & 1) << (move.To);
				position->P2 |= (TBB)((move.Prom) >> 2) << (move.To);
				position->enPassant = 8; /* clear enpassant */
			}
			else /* capture or push */
			{
				position->PM ^= part | dest;
				position->P0 ^= part | dest;
				position->enPassant = 8; /* clear enpassant */
				if (move.To == move.From + 16 && EnPassantM[move.To & 0x07] & Pawns & (position->PM ^ (Occupation)))
					position->enPassant = move.To & 0x07; /* save enpassant column */
			}
			if (move.MoveType & CAPTURE)
			{
				if (CastleEK && move.To == 63) ResetCastleEK; /* captured the opponent king side rook */
				else if (CastleEQ && move.To == 56) ResetCastleEQ; /* captured the opponent quuen side rook */
			}
		}
		position->move50 = 0;
		ChangeSide;
		break;
	case KNIGHT:
	case BISHOP:
	case ROOK:
	case QUEEN:
		if (move.MoveType & CAPTURE)
		{
			position->P0 &= ~dest;
			position->P1 &= ~dest;
			position->P2 &= ~dest;
		}
		position->PM ^= part | dest;
		position->P0 ^= (move.MoveType & 1) ? part | dest : 0;
		position->P1 ^= (move.MoveType & 2) ? part | dest : 0;
		position->P2 ^= (move.MoveType & 4) ? part | dest : 0;
		position->enPassant = 8;
		if ((move.MoveType & 0x7) == ROOK) /* update the castle rights */
		{
			if (CastleMK && move.From == 7) ResetCastleMK;
			else if (CastleMQ && move.From == 0) ResetCastleMQ;
		}
		if (move.MoveType & CAPTURE) /* update the castle rights */
		{
			if (CastleEK && move.To == 63) ResetCastleEK;
			else if (CastleEQ && move.To == 56) ResetCastleEQ;
			position->move50 = 0;
		}
		ChangeSide;
		if (!(move.MoveType & CAPTURE))
			position->move50++;
		else position->move50 = 0;
		break;
	case KING:
		if (move.MoveType & CAPTURE)
		{
			position->P0 &= ~dest;
			position->P1 &= ~dest;
			position->P2 &= ~dest;
		}
		position->PM ^= part | dest;
		position->P1 ^= part | dest;
		position->P2 ^= part | dest;
		if (CastleMK) ResetCastleMK; /* update the castle rights */
		if (CastleMQ) ResetCastleMQ;
		position->enPassant = 8;
		if (move.MoveType & CAPTURE)
		{
			if (CastleEK && move.To == 63) ResetCastleEK;
			else if (CastleEQ && move.To == 56) ResetCastleEQ;
			position->move50 = 0;
		}
		else if (move.MoveType & CASTLE)
		{
			if (move.To == 6)
			{
				position->PM ^= 0x00000000000000A0ULL; position->P2 ^= 0x00000000000000A0ULL;
			} /* short castling */
			else
			{
				position->P2 ^= 0x0000000000000009ULL; position->PM ^= 0x0000000000000009ULL;
			} /* long castling */
		}
		ChangeSide;
		if (!(move.MoveType & CAPTURE))
			position->move50++;
		else position->move50 = 0;
	default: break;
	}
}

/* Evaluate the leaf positions */
static inline int Evaluate(){
	int eval = 0;
	int gamephase = PopCount(Knights | Bishops | Rooks | Queens | Kings);
	for (int sides = 0; sides < 2; sides++) /* evaluate the 2 sides */
	{
		/* for every piece sum the static value and the pst value */
		for (TPieceType pt = PAWN; pt <= QUEEN; pt++)
			for (TBB pieces = BBPieces(pt) & position->PM; pieces; pieces = ClearLSB(pieces))
				eval += PST[pt][LSB(pieces)] + StaticValue[pt];
		/* interpolate the 2 pst of the king */
		uint64_t kingsq = LSB(Kings & position->PM);
		eval += (PST[KING][kingsq] * gamephase + PST[KING + 1][kingsq] * (16 - gamephase)) / 16;
		ChangeSide;
		eval = -eval;
	}
	return eval;
}

static int Permill() {
	int pm = 0;
	for (int n = 0; n < 1000; n++)
		if (tt[n].hash)
			pm++;
	return pm;
}

static U64 GetHash() {
	U64 hash = position->STM;
	U64 copy = Occupation;
	while (copy) {
		const int sq = LSB(copy);
		copy &= copy - 1;
		hash ^= keys[Piece(sq) * 64 + sq];
	}
	if (position->enPassant < 8)
		hash ^= keys[position->enPassant];
	if (position->castleFlags)
		hash ^= keys[8 + position->castleFlags];
	return hash;
}

static int IsPseudolegalMove(const TMove move) {
	TMove moves[256];
	const int num_moves = GenerateMoves(moves, 0);
	for (int i = 0; i < num_moves; ++i)
		if (moves[i].Move == move.Move)
			return 1;
	return 0;
}

static void PrintPv(const TMove move) {
	if (!IsPseudolegalMove(move))
		return;
	if (Illegal(move))
		return;
	char strmove[8];
	MoveToStr(strmove, move, position->STM);
	printf(" %s", strmove);
	Make(move);
	const U64 hash = GetHash();
	TTEntry* ttEntry = tt + (hash % TT_SIZE);
	if (ttEntry->hash == hash && !IsRepetition(hash)) {
		historyHash[historyCount++] = hash;
		PrintPv(ttEntry->move);
		historyCount--;
	}
	position--;
}

static void PrintInfo(int depth, int score) {
	printf("info depth %d score ", depth);
	if (abs(score) < MATE - MAX_PLY)
		printf("cp %d", score);
	else
		printf("mate %d", (score > 0 ? (MATE - score + 1) >> 1 : -(MATE + score) >> 1));
	printf(" time %lld", GetTimeMs() - info.timeStart);
	printf(" nodes %lld", info.nodes);
	printf(" hashfull %d pv", Permill());
	PrintPv(ss[0].move);
	printf("\n");
}

static int IsRepetition(U64 hash) {
	int limit = max(0, historyCount - position->move50);
	for (int n = historyCount - 4; n >= limit; n -= 2)
		if (historyHash[n] == hash)
			return TRUE;
	return FALSE;
}

static void PrintBoard(){
	U64 hash = GetHash();
	int color = position->STM == WHITE ? 0 : 1;
	if (color)
		ChangeSide;
	const char* s = "   +---+---+---+---+---+---+---+---+\n";
	const char* t = "     A   B   C   D   E   F   G   H\n";
	printf(t);
	for (int r = 7; r >= 0; r--) {
		printf(s);
		printf(" %d |", r + 1);
		for (int f = 0; f < 8; f++) {
			int sq = r * 8 + f;
			int pt = PieceType(sq);
			if (position->PM & (1ull << sq))
				printf(" %c |", " ANBRQK"[pt]);
			else
				printf(" %c |", " anbrqk"[pt]);
		}
		printf(" %d \n", r + 1);
	}
	printf(s);
	printf(t);
	char castling[5] = "KQkq";
	for (int n = 0; n < 4; n++)
		if (!(position->castleFlags & (1 << n)))
			castling[n] = '-';
	printf("side     : %16s\n", color ? "black" : "white");
	printf("castling : %16s\n", castling);
	printf("hash     : %16llx\n",hash);
	if (color)
		ChangeSide;
}

static int SearchAlpha(int alpha, int beta, int depth, int ply) {
	if (CheckUp())
		return 0;
	int  mateValue = MATE - ply;
	if (alpha < -mateValue)
		alpha = -mateValue;
	if (beta > mateValue - 1)
		beta = mateValue - 1;
	if (alpha >= beta)
		return alpha;

	const U64 hash = GetHash();
	TTEntry* ttEntry = tt + (hash % TT_SIZE);
	TMove tt_move = { 0 };
	int inPv = beta - alpha > 1;
	if (ttEntry->hash == hash) {
		tt_move = ttEntry->move;
		if (!inPv && ttEntry->depth >= depth) {
			//printf("%16llx    : %16llx\n", hash, hash % TT_SIZE);
			if (ttEntry->flag == EXACT)return ttEntry->score;
			if (ttEntry->flag == LOWER && ttEntry->score <= alpha)return ttEntry->score;
			if (ttEntry->flag == UPPER && ttEntry->score >= beta)return ttEntry->score;
		}
	}
	else
		depth -= depth > 3;

	int inCheck = InCheck();
	if (inCheck)
		depth = max(1, depth + 1);
	int inQSearch = depth < 1;
	if (ply && !inQSearch)
		if (position->move50 >= 100 || IsRepetition(hash))
			return 0;
	const int staticEval = Evaluate();
	if (ply >= MAX_PLY)
		return staticEval;
	if (inQSearch && alpha < staticEval) {
		alpha = staticEval;
		if (alpha >= beta)
			return beta;
	}

	historyHash[historyCount++] = hash;
	int score;
	int color = position->STM == WHITE ? 0 : 1;
	U8 ttFlag = LOWER;
	int legalMoves = 0;
	S64 scoreList[256];
	TMove movesList[256];
	int movesCount = GenerateMoves(movesList, inQSearch);
	for (int j = 0; j < movesCount; ++j) {
		TMove m = movesList[j];
		const int ptSou = PieceType(m.From);
		int ptDes = m.Prom ? m.Prom : PieceType(m.To);
		if (m.Move == tt_move.Move)
			scoreList[j] = 1LL << 62;
		else if (ptDes != PT_NB)
			scoreList[j] = ((ptDes + 1) * (1LL << 54)) - ptSou;
		else if (m.Move == ss[ply].killer1.Move)
			scoreList[j] = 1LL << 50;
		else if (m.Move == ss[ply].killer2.Move)
			scoreList[j] = 1LL << 48;
		else
			scoreList[j] = hh[color][m.From][m.To];
	}
	for (int i = 0; i < movesCount; ++i) {
		int bstIdx = i;
		for (int j = i + 1; j < movesCount; ++j)
			if (scoreList[bstIdx] < scoreList[j])
				bstIdx = j;
		TMove move = movesList[bstIdx];
		scoreList[bstIdx] = scoreList[i];
		movesList[bstIdx] = movesList[i];
		if (Illegal(move))
			continue;
		Make(move);

		if (!legalMoves || depth < 4)
			score = -SearchAlpha(-beta, -alpha, depth - 1, ply + 1);
		else {
			int r = !inPv;
			score = -SearchAlpha(-alpha - 1, -alpha, depth - 1 - r, ply + 1);
			if (r && score > alpha)
				score = -SearchAlpha(-alpha - 1, -alpha, depth - 1, ply + 1);
			if (score > alpha && score < beta)
				score = -SearchAlpha(-beta, -alpha, depth - 1, ply + 1);
		}

		position--; /* unmake the move bringing the previous position */
		legalMoves++; /* increment the number of legal moves found */
		if (info.stop)
			break;
		if (alpha < score) {
			alpha = score;
			ttFlag = EXACT;
			ss[ply].move = move; /* save the best move found at this ply */
			if (!ply && info.post)
				PrintInfo(depth, score);
		}
		if (alpha >= beta) {
			ttFlag = UPPER;
			if (!(move.MoveType & (CAPTURE | PROMO))) {
				ss[ply].killer2 = ss[ply].killer1;
				ss[ply].killer1 = move;
			}
			int bonus = depth * depth;
			int h = hh[color][move.From][move.To];
			h += bonus - h / 1024;
			hh[color][move.From][move.To] = h;
			break;
		}
	}

	historyCount--;
	if (info.stop)
		return 0;
	if (!legalMoves && !inQSearch)
		return inQSearch ? alpha : inCheck ? ply - MATE : 0;
	ttEntry->hash = hash;
	ttEntry->move = ss[ply].move;
	ttEntry->depth = max(0, depth);
	ttEntry->score = alpha;
	ttEntry->flag = ttFlag;
	return alpha;
}

static void SearchIteratively() {
	TTClear();
	SSClear();
	int score = 0;
	int alpha = -MATE;
	int beta = MATE;
	for (int depth = 1; depth <= info.depthLimit; ++depth) {
		int aspH = 16, aspL = 16;
		do {
			if (depth > 4) {
				alpha = score - aspL;
				beta = score + aspH;
			}
			score = SearchAlpha(alpha, beta, depth, 0);
			if (score <= alpha) {
				alpha -= aspL;
				aspL *= 2;
			}
			else if (score >= beta) {
				beta += aspH;
				aspH *= 2;
			}
			else
				break;
		} while (!info.stop);
		if (info.stop)
			break;
		if (info.timeLimit && GetTimeMs() - info.timeStart > info.timeLimit / 2)
			break;
	}
	if (info.post) {
		char strmove[8];
		MoveToStr(strmove, ss[0].move, position->STM);
		printf("bestmove %s\n", strmove);
		fflush(stdout);
	}
}

/*
Load a position starting from a fen and a list of moves.
This function doesn't check the correctness of the fen and the moves sent.
*/
static void SetFen(const char* fen)
{
	/* Clear the board */
	position = Game;
	position->P0 = position->P1 = position->P2 = position->PM = 0;
	position->enPassant = 8;
	position->STM = WHITE;
	position->move50 = 0;
	position->castleFlags = 0;

	/* translate the fen to the relative position */
	uint8_t pieceside = WHITE;
	uint8_t piece = PAWN;
	uint64_t square = 0;
	const char* cursor;
	for (cursor = fen; *cursor != ' '; cursor++)
	{
		if (*cursor >= '1' && *cursor <= '8') square += *cursor - '0';
		else if (*cursor == '/') continue;
		else
		{
			uint64_t pos = OppSq(square);
			if (*cursor == 'p') { piece = PAWN; pieceside = BLACK; }
			else if (*cursor == 'n') { piece = KNIGHT; pieceside = BLACK; }
			else if (*cursor == 'b') { piece = BISHOP; pieceside = BLACK; }
			else if (*cursor == 'r') { piece = ROOK; pieceside = BLACK; }
			else if (*cursor == 'q') { piece = QUEEN; pieceside = BLACK; }
			else if (*cursor == 'k') { piece = KING; pieceside = BLACK; }
			else if (*cursor == 'P') { piece = PAWN; pieceside = WHITE; }
			else if (*cursor == 'N') { piece = KNIGHT; pieceside = WHITE; }
			else if (*cursor == 'B') { piece = BISHOP; pieceside = WHITE; }
			else if (*cursor == 'R') { piece = ROOK; pieceside = WHITE; }
			else if (*cursor == 'Q') { piece = QUEEN; pieceside = WHITE; }
			else if (*cursor == 'K') { piece = KING; pieceside = WHITE; }
			position->P0 |= ((uint64_t)piece & 1) << pos;
			position->P1 |= ((uint64_t)(piece >> 1) & 1) << pos;
			position->P2 |= ((uint64_t)piece >> 2) << pos;
			if (pieceside == WHITE) { position->PM |= 1ULL << pos; piece |= BLACK; }
			square++;
		}
	}
	cursor++; /* read the side to move  */
	U8 sidetomove = *cursor == 'w' ? WHITE : BLACK;
	cursor += 2;
	if (*cursor != '-') /* read the castle rights */
	{
		for (; *cursor != ' '; cursor++)
		{
			if (*cursor == 'K')
				position->castleFlags |= CASTLE_WK;
			else if (*cursor == 'Q')
				position->castleFlags |= CASTLE_WQ;
			else if (*cursor == 'k')
				position->castleFlags |= CASTLE_BK;
			else if (*cursor == 'q')
				position->castleFlags |= CASTLE_BQ;
		}
		cursor++;
	}
	else cursor += 2;
	if (*cursor != '-') /* read the enpassant column */
	{
		position->enPassant = *cursor - 'a';
		cursor++;
	}
	else cursor += 2;
	char counter50moves[4];
	char* pcounter;
	for (pcounter = counter50moves; *cursor != ' '; cursor++, pcounter++) *pcounter = *cursor; /* copy the string */
	*pcounter = '\0';
	position->move50 = atoi(counter50moves); /* convert the string counter to integer */
	if (sidetomove == BLACK) ChangeSide;
}

static inline void PerftDriver(int depth) {
	TMove moves[256];
	const int numMoves = GenerateMoves(moves, 0);
	for (int n = 0; n < numMoves; n++) {
		TMove move = moves[n];
		if (Illegal(move))
			continue;
		if (depth) {
			Make(move);
			PerftDriver(depth - 1);
			position--;
		}
		else
			info.nodes++;
	}
}

static int ShrinkNumber(U64 n) {
	if (n < 10000)
		return 0;
	if (n < 10000000)
		return 1;
	if (n < 10000000000)
		return 2;
	return 3;
}

static void PrintSummary(U64 time, U64 nodes) {
	U64 nps = (nodes * 1000) / max(time, 1);
	const char* units[] = { "", "k", "m", "g" };
	int sn = ShrinkNumber(nps);
	int p = pow(10, sn * 3);
	int b = pow(10, 3);
	printf("-----------------------------\n");
	printf("Time        : %llu\n", time);
	printf("Nodes       : %llu\n", nodes);
	printf("Nps         : %llu (%llu%s/s)\n", nps, nps / p, units[sn]);
	printf("-----------------------------\n");
}

static void PrintPerformanceHeader() {
	printf("-----------------------------\n");
	printf("ply      time        nodes\n");
	printf("-----------------------------\n");
}

static void ResetInfo() {
	info.timeStart = GetTimeMs();
	info.timeLimit = 0;
	info.depthLimit = MAX_PLY;
	info.nodesLimit = 0;
	info.nodes = 0;
	info.stop = FALSE;
	info.post = TRUE;
}

//performance test
static void UciPerformance() {
	ResetInfo();
	PrintPerformanceHeader();
	info.depthLimit = 0;
	U64 elapsed = 0;
	while (elapsed < 3000) {
		PerftDriver(info.depthLimit++);
		elapsed = GetTimeMs() - info.timeStart;
		printf(" %2d. %8llu %12llu\n", info.depthLimit, elapsed, info.nodes);
	}
	PrintSummary(elapsed, info.nodes);
}

//start benchmark
static void UciBench() {
	ResetInfo();
	PrintPerformanceHeader();
	info.depthLimit = 0;
	info.post = FALSE;
	U64 elapsed = 0;
	while (elapsed < 3000) {
		++info.depthLimit;
		SearchIteratively();
		elapsed = GetTimeMs() - info.timeStart;
		printf(" %2d. %8llu %12llu\n", info.depthLimit, elapsed, info.nodes);
	}
	PrintSummary(elapsed, info.nodes);
}

static void SetMoves(char* str) {
	char buffer[4000];
	strcpy(buffer, str);
	char* strmove;
	for (strmove = strtok(buffer, " "); strmove; strmove = strtok(NULL, " ")) {
		TMove move = StrToMove(strmove);
		Make(move);
		if (position->move50 == 0) {  /* if the counter is zeroed we can put this position in the beginnig of the array because there can't be repetitions before */
			Game[0] = *position;
			position = Game;
		}
	}
}

static void ParsePosition(char* str) {
	char* fen = strstr(str, "fen");
	char* mov = strstr(str, "moves");
	if (!fen)
		fen = START_FEN;
	else
		fen += 4;
	SetFen(fen);
	if (mov)
		SetMoves(mov + 6);
}

static void ParseGo(char* command) {
	ResetInfo();
	int wtime = 0;
	int btime = 0;
	int winc = 0;
	int binc = 0;
	int movestogo = 32;
	char* argument = NULL;
	if (argument = strstr(command, "binc"))
		binc = atoi(argument + 5);
	if (argument = strstr(command, "winc"))
		winc = atoi(argument + 5);
	if (argument = strstr(command, "wtime"))
		wtime = max(1, atoi(argument + 6));
	if (argument = strstr(command, "btime"))
		btime = max(1, atoi(argument + 6));
	if ((argument = strstr(command, "movestogo")))
		movestogo = atoi(argument + 10);
	if ((argument = strstr(command, "movetime")))
		info.timeLimit = atoi(argument + 9);
	if ((argument = strstr(command, "depth")))
		info.depthLimit = atoi(argument + 6);
	if (argument = strstr(command, "nodes"))
		info.nodesLimit = atoi(argument + 5);
	int time = position->STM ? btime : wtime;
	int inc = position->STM ? binc : winc;
	if (time)
		info.timeLimit = max(1, min(time / movestogo + inc, time / 2));
	SearchIteratively();
}

void UciCommand(char* str) {
	if (!strncmp(str, "ucinewgame", 10));
	else if (!strncmp(str, "uci", 3)) {
		printf("id name %s\nuciok\n", NAME);
		fflush(stdout);
	}
	else if (!strncmp(str, "isready", 7)) {
		printf("readyok\n");
		fflush(stdout);
	}
	else if (!strncmp(str, "go", 2))ParseGo(str + 2);
	else if (!strncmp(str, "position", 8))ParsePosition(str + 8);
	else if (!strncmp(str, "print", 5))PrintBoard();
	else if (!strncmp(str, "perft", 5))UciPerformance();
	else if (!strncmp(str, "bench", 5))UciBench();
	else if (!strncmp(str, "stop", 4))info.stop = TRUE;
	else if (!strncmp(str, "quit", 4))exit(0);
}

static void UciLoop() {
	//TestPerft();
	//UciCommand("position fen r2qk2r/ppp2ppp/3b1n2/8/4P3/1PP2b2/P1Q2P1P/RNB1KB1R b KQkq - 0 10 moves f3h1 f1b5");
	// 
	//UciCommand("position fen 8/3P4/1p3b1p/p7/P7/1P3NPP/4p1K1/3k4 w - - 0 1");
	// 
	//UciCommand("position startpos moves e2e4 b8c6 d2d4 e7e5 d4e5 d7d6 e5d6 f8d6 c2c3 g8f6 g2g4 c8g4 d1d3 c6e5 d3c2 e5f3 g1f3 g4f3 b2b3 f3h1 f1b5 c7c6 c1g5 c6b5 g5e3 h1e4 c2e2 e8g8 a2a3 f6d5");
	//UciCommand("position startpos moves e2e4 b8c6 d2d4 e7e5 d4e5 d7d6 e5d6 f8d6 c2c3 g8f6 g2g4 c8g4 d1d3 c6e5 d3c2 e5f3 g1f3 g4f3 b2b3 f3h1 f1b5");
	//UciCommand("print");
	//UciCommand("go movetime 1000");
	//UciCommand("go depth 6");
	char str[4000];
	while (fgets(str, sizeof(str), stdin))
		UciCommand(str);
}

int main(const int argc, const char** argv) {
	InitHash();
	printf("%s %s\n", NAME, VERSION);
	SetFen(START_FEN);
	UciLoop();
}