//
// file: TKeyboard.h
// Arduino STM32用 PS/2 キーボード制御 by たま吉さん
// 作成日 2017/01/31
// 修正日 2017/02/01, USキーボードの定義ミス修正
// 修正日 2017/02/02, ctrl_LED()のバグ修正
// 修正日 2017/02/03, ctrl_LED()のレスポンス受信処理の修正
// 修正日 2017/02/05, setPriority()関数の追加、ctrl_LED()の処理方式変更
// 修正日 2017/02/05, KEY_SPACEのキーコード変更(32=>35)対応
// 修正日 2017/04/10, NumLock+編集キーの不具合暫定対応
// 修正日 2018/08/22, キーコードマクロ名をKEY_XXXからPS2_XXXに変更(文字コードとの混乱回避のため）
//

#include <TKeyboard.h>

static TPS2 pb; // PS/2 I/F オブジェクト
volatile static uint8_t _flgLED; // LED制御利用フラグ(true:利用する false:利用しない)
volatile static uint8_t _flgUS;  // USキーボードの利用
volatile static uint8_t (*key_ascii)[2];
   
// スキャンコード解析状態遷移コード
#define STS_SYOKI          0  // 初期
#define STS_1KEY           1   // 1バイト(END) [1]
#define STS_1KEY_BREAK     2   // BREAK[2]
#define STS_1KEY_BREAK_1   3   // BREAK＋1バイト(END)[2-1]
#define STS_MKEY_1         4   // マルチバイト1バイト目[3]
#define STS_MKEY_2         5   // マルチバイト2バイト目(END)[3-1]
#define STS_MKEY_BREAK     6   // マルチバイト1バイト目+BREAK[3-2]
#define STS_MKEY_BREAK_1   7   // マルチバイト1バイト目＋BREAK+2バイト目(END)[3-2-1]
#define STS_MKEY_BREAK_SC2 8   // マルチバイト1バイト目+BREAK+prnScrn2バイト目[3-2-2]
#define STS_MKEY_SC2       9   // マルチバイト1バイト目+prnScrn2バイト目[3-3]
#define STS_MKEY_SC3      10   // マルチバイト1バイト目+prnScrn3バイト目[3-3-1]
#define STS_MKEY_PS       11   // pauseキー1バイト目[4]

// ロックキーの状態
// ※0ビット目でLOCKのON/OFFを判定している
#define LOCK_Start    B00 // 初期 
#define LOCK_ON_Make  B01 // LOCK ON キーを押したままの状態
#define LOCK_ON_Break B11 // LOCK ON キーを離した状態
#define LOCK_OFF_Make B10 // LOCK OFF キーを押したままの状態


// スキャンコード 0x00-0x83 => キーコード変換テーブル
// 132バイト分
static uint8_t keycode1[] __FLASH__ = {
 0            , PS2_F9      , 0           , PS2_F5         , PS2_F3        , PS2_F1         , PS2_F2        , PS2_F12,       // 0x00-0x07
 PS2_F13      , PS2_F10     , PS2_F8      , PS2_F6         , PS2_F4        , PS2_Tab        , PS2_HanZen    , PS2_PAD_Equal, // 0x08-0x0F
 PS2_F14      , PS2_L_Alt   , PS2_L_Shift , PS2_Romaji     , PS2_L_Ctrl    , PS2_Q          , PS2_1         , 0,             // 0x10-0x17
 PS2_F15      , 0           , PS2_Z       , PS2_S          , PS2_A         , PS2_W          , PS2_2         , 0,             // 0x18-0x1F
 PS2_F16      , PS2_C       , PS2_X       , PS2_D          , PS2_E         , PS2_4          , PS2_3         , 0,             // 0x20-0x27
 PS2_F17      , PS2_Space   , PS2_V       , PS2_F          , PS2_T         , PS2_R          , PS2_5         , 0,             // 0x28-0x2F
 PS2_F18      , PS2_N       , PS2_B       , PS2_H          , PS2_G         , PS2_Y          , PS2_6         , 0,             // 0x30-0x37
 PS2_F19      , 0           , PS2_M       , PS2_J          , PS2_U         , PS2_7          , PS2_8         , 0,             // 0x38-0x3F
 PS2_F20      , PS2_Kamma   , PS2_K       , PS2_I          , PS2_O         , PS2_0          , PS2_9         , 0,             // 0x40-0x47
 PS2_F21      , PS2_Dot     , PS2_Question, PS2_L          , PS2_Semicolon , PS2_P          , PS2_minus     , 0,             // 0x48-0x4F
 PS2_F22      , PS2_Ro      , PS2_Colon   , 0              , PS2_AT        , PS2_Hat        , 0             , PS2_F23,       // 0x50-0x57
 PS2_CapsLock , PS2_R_Shift , PS2_Enter   , PS2_L_brackets , 0             , PS2_R_brackets , 0             , 0,             // 0x58-0x5F
 0            , PS2_Pipe2   , 0           , 0              , PS2_Henkan    , 0              , PS2_Backspace , PS2_Muhenkan,  // 0x60-0x67
 0            , PS2_PAD_1   , PS2_Pipe    , PS2_PAD_4      , PS2_PAD_7     , PS2_PAD_Kamma  , 0             , 0,             // 0x68-0x6F
 PS2_PAD_0    , PS2_PAD_DOT , PS2_PAD_2   , PS2_PAD_5      , PS2_PAD_6     , PS2_PAD_8      , PS2_ESC       , PS2_NumLock,   // 0x70-0x77
 PS2_F11      , PS2_PAD_Plus, PS2_PAD_3   , PS2_PAD_Minus  , PS2_PAD_Multi , PS2_PAD_9      , PS2_ScrollLock, 0,             // 0x78-0x7F
 0            , 0           , 0           , PS2_F7         ,                                                                 // 0x80-0x83
}; 

// スキャンコード 0xE010-0xE07D => キーコード変換テーブル
// 2バイト(スキャンコード下位1バイト, キーコード) x 38
static uint8_t keycode2[][2] __FLASH__ = {
 { 0x10 , PS2_WWW_Search },    { 0x11 , PS2_R_Alt },      { 0x14 , PS2_R_Ctrl },      { 0x15 , PS2_PrevTrack },
 { 0x18 , PS2_WWW_Favorites }, { 0x1F , PS2_L_GUI },      { 0x20 , PS2_WWW_Refresh }, { 0x21 , PS2_VolumeDown },
 { 0x23 , PS2_Mute },          { 0x27 , PS2_R_GUI },      { 0x28 , PS2_WWW_Stop },    { 0x2B , PS2_Calc },
 { 0x2F , PS2_APP },           { 0x30 , PS2_WWW_Forward },{ 0x32 , PS2_VolumeUp },    { 0x34 , PS2_PLAY },
 { 0x37 , PS2_POWER },         { 0x38 , PS2_WWW_Back },   { 0x3A , PS2_WWW_Home },    { 0x3B , PS2_Stop },
 { 0x3F , PS2_Sleep },         { 0x40 , PS2_Mycomputer }, { 0x48 , PS2_Mail },        { 0x4A , PS2_PAD_Slash },
 { 0x4D , PS2_NextTrack },     { 0x50 , PS2_MEdiaSelect },{ 0x5A , PS2_PAD_Enter },   { 0x5E , PS2_Wake },
 { 0x69 , PS2_End },           { 0x6B , PS2_L_Arrow },    { 0x6C , PS2_Home },        { 0x70 , PS2_Insert },
 { 0x71 , PS2_Delete },        { 0x72 , PS2_Down_Arrow }, { 0x74 , PS2_R_Arrow },     { 0x75 , PS2_Up_Arrow },
 { 0x7A , PS2_PageDown },      { 0x7D , PS2_PageUp },
};

// Pause Key スキャンコード
static uint8_t pausescode[] __FLASH__ =  {0xE1,0x14,0x77,0xE1,0xF0,0x14,0xF0,0x77};

// PrintScreen Key スキャンコード(break);
static uint8_t prnScrncode2[] __FLASH__ = {0xE0,0xF0,0x7C,0xE0,0xF0,0x12};   // Break用


// キーコード・ASCIIコード変換テーブル
// {シフト無しコード, シフトありコード }

// USキーボード
static uint8_t key_ascii_us[][2] __FLASH__ = {
  /*{0x1B,0x1B},{0x09,0x09},{0x0D,0x0D},{0x08,0x08},{0x7F,0x7F},*/
  { ' ', ' '},{ ',','\"'},{ ';', ':'},{ ',', '<'},{ '-', '_'},{ '.', '>'},{ '/', '?'},{ '[', '{'},
  { ']', '}'},{ '\\','|'},{'\\', '|'},{ '=', '+'},{ '\\','_'},{ '0', ')'},{ '1', '!'},{ '2', '@'},
  { '3', '#'},{ '4', '$'},{ '5', '%'},{ '6', '^'},{ '7', '&'},{ '8', '*'},{ '9', '('},{'\\' ,'|'},
  {  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{ 'a', 'A'},{ 'b', 'B'},
  { 'c', 'C'},{ 'd', 'D'},{ 'e', 'E'},{ 'f', 'F'},{ 'g', 'G'},{ 'h', 'H'},{ 'i', 'I'},{ 'j', 'J'},
  { 'k', 'K'},{ 'l', 'L'},{ 'm', 'M'},{ 'n', 'N'},{ 'o', 'O'},{ 'p', 'P'},{ 'q', 'Q'},{ 'r', 'R'},
  { 's', 'S'},{ 't', 'T'},{ 'u', 'U'},{ 'v', 'V'},{ 'w', 'W'},{ 'x', 'X'},{ 'y', 'Y'},{ 'z', 'Z'},
};

//（日本語キーボード)
static uint8_t key_ascii_jp[][2] __FLASH__ = {
  /*{0x1B,0x1B},{0x09,0x09},{0x0D,0x0D},{0x08,0x08},{0x7F,0x7F},*/
  { ' ', ' '},{ ':', '*'},{ ';', '+'},{ ',', '<'},{ '-', '='},{ '.', '>'},{ '/', '?'},{ '@', '`'},
  { '[', '{'},{ '\\','|'},{ ']', '}'},{ '^', '~'},{ '\\','_'},{ '0', 0  },{ '1', '!'},{ '2','\"'},
  { '3', '#'},{ '4', '$'},{ '5', '%'},{ '6', '&'},{ '7','\''},{ '8', '('},{ '9', ')'},{  0 ,  0 },
  {  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{  0 ,  0 },{ 'a', 'A'},{ 'b', 'B'},
  { 'c', 'C'},{ 'd', 'D'},{ 'e', 'E'},{ 'f', 'F'},{ 'g', 'G'},{ 'h', 'H'},{ 'i', 'I'},{ 'j', 'J'},
  { 'k', 'K'},{ 'l', 'L'},{ 'm', 'M'},{ 'n', 'N'},{ 'o', 'O'},{ 'p', 'P'},{ 'q', 'Q'},{ 'r', 'R'},
  { 's', 'S'},{ 't', 'T'},{ 'u', 'U'},{ 'v', 'V'},{ 'w', 'W'},{ 'x', 'X'},{ 'y', 'Y'},{ 'z', 'Z'},
};

// テンキー用変換テーブル 94～111
// {通常コード, NumLock/Shift時コード, KEYコード(=1)/ASCII(=0)区分 }
static uint8_t tenkey[][3]  __FLASH__ = {
  { '=',  '='            ,0},
  { 0x0D, 0x0D           ,0},
  { '0',  PS2_Insert     ,1},
  { '1',  PS2_End        ,1},
  { '2',  PS2_Down_Arrow ,1},
  { '3',  PS2_PageDown   ,1},
  { '4',  PS2_L_Arrow    ,1},
  { '5',  '5'            ,0},
  { '6',  PS2_R_Arrow    ,1},
  { '7',  PS2_Home       ,1},
  { '8',  PS2_Up_Arrow   ,1},
  { '9',  PS2_PageUp     ,1},
  { '*',  '*'            ,0},
  { '+',  '+'            ,0},
  { ',',  ','            ,1},
  { '-',  PS2_PAD_Minus  ,1},
  { '.',  0x7f           ,0},
  { '/',  '/'            ,0},
};

//
// 利用開始(初期化)
// 引数
//   clk    : PS/2 CLK
//   dat    : PS/2 DATA
//   flgLED : false LED制御をしない、true LED制御を行う
// 戻り値
//  0:正常終了 0以外: 異常終了
uint8_t TKeyboard::begin(uint8_t clk, uint8_t dat, uint8_t flgLED, uint8_t flgUS) {
  uint8_t rc;
  _flgUS = flgUS;
  if (_flgUS)
    key_ascii = key_ascii_us;
  else
    key_ascii = key_ascii_jp;
  
  _flgLED = flgLED;    // LED制御利用有無
  pb.begin(clk, dat);  // PS/2インタフェースの初期化
  rc = init();         // キーボードの初期化
  return rc;
}

// 利用終了
void TKeyboard::end() {
  pb.end();
}

// キーボード初期化
// 戻り値 0:正常終了、 0以外:異常終了
uint8_t TKeyboard::init() {
  uint8_t c,err;
  
  pb.disableInterrupts();
  pb.clear_queue();
          
  // リセット命令:FF 応答がFA,AAなら
  if ( (err = pb.hostSend(0xff)) )    goto ERROR;
  err = pb.response(&c);  if (err || (c != 0xFA)) goto ERROR;  
  err = pb.response(&c);  if (err || (c != 0xAA)) goto ERROR;   

  // 識別情報チェック:F2 応答がキーボード識別のFA,AB,83ならOK
  if ( (err = pb.hostSend(0xf2)) )   goto ERROR;
  err = pb.response(&c);  if (err || (c != 0xFA)) goto ERROR;  
  err = pb.response(&c);  if (err || (c != 0xAB)) goto ERROR;  
  err = pb.response(&c);  if (err || (c != 0x83)) goto ERROR;  

ERROR:
  pb.mode_idole(TPS2::D_IN); 
  pb.enableInterrupts();
  return err;
}

// CLK変化割り込み許可
void TKeyboard::enableInterrupts() {
  pb.enableInterrupts();
}

// CLK変化割り込み禁止
void TKeyboard::disableInterrupts() {
  pb.disableInterrupts();
}

// 割り込み優先度の設定
void TKeyboard::setPriority(uint8_t n) {
  pb.setPriority(n);
}

// PS/2ラインをアイドル状態に設定
void TKeyboard::mode_idole() {
  pb.mode_idole(TPS2::D_IN);
}

// PS/2ラインを通信禁止
void TKeyboard::mode_stop() {
  pb.mode_stop();
}

// PS/2ラインをホスト送信モード設定
void TKeyboard::mode_send() {
  pb.mode_send();
}
    
// スキャンコード検索
uint8_t TKeyboard::findcode(uint8_t c)  {
 int  t_p = 0; //　検索範囲上限
 int  e_p = sizeof(keycode2)/sizeof(keycode2[0])-1; //  検索範囲下限
 int  pos;
 uint16_t  d = 0;
 int flg_stop = 0;
 
 while(true) {
    pos = t_p + ((e_p - t_p+1)>>1);
    d = keycode2[pos][0];
   if (d == c) {
     // 等しい
     flg_stop = 1;
     break;
   } else if (c > d) {
     // 大きい
     t_p = pos + 1;
     if (t_p > e_p) {
       break;
     }
   } else {
     // 小さい
    e_p = pos -1;
    if (e_p < t_p) 
      break;
   }
 } 
 if (!flg_stop)
    return 0;
 return keycode2[pos][1];    
}

// 
// スキャンコードからキーコードへの変換
// 戻り値
//   正常時
//     下位8ビット: キーコード 
//     上位1ビット: 0:MAKE 1:BREAK
//   キー入力なし
//     0
//   エラー
//     255
//
uint16_t TKeyboard::scanToKeycode() {
  static uint8_t state = STS_SYOKI;
  static uint8_t scIndex = 0;
  uint16_t c, code = 0;

  while(pb.available()) {
    c = pb.dequeue();     // キューから1バイト取り出し
  	//Serial.print("S=");
  	//Serial.println(c,HEX);
    switch(state) {
    case STS_SYOKI: // [0]->
      if (c <= 0x83) {
        // [0]->[1] 1バイト(END) 
        code = keycode1[c];
        goto DONE;
      } else {
        switch(c) {
        case 0xF0:  state = STS_1KEY_BREAK; continue; // ->[2]
        case 0xE0:  state = STS_MKEY_1; continue;     // ->[3]
        case 0xE1:  state = STS_MKEY_PS;scIndex = 0; continue;  // ->[4]
        default:    goto STS_ERROR;  // -> ERROR
        }
      }
      break;

    case STS_1KEY_BREAK: // [2]->
      if (c <= 0x83) {
        // [2]->[2-1] BREAK+1バイト(END) 
        code = keycode1[c] | BREAK_CODE;
        goto DONE;
      } else {
        goto STS_ERROR; // -> ERROR
      }
      break;

    case STS_MKEY_1: // [3]->
      switch(c) {
      case 0xF0:  state = STS_MKEY_BREAK; continue; // ->[3-2]
      case 0x12:  state = STS_MKEY_SC2;scIndex=1; continue; // ->[3-3]
      default:
          code = findcode(c);
          if (code) {
            // [3]->[3-1] マルチバイト2バイト目(END)
            goto DONE; 
          } else {
            goto STS_ERROR; // -> ERROR
          }
      }
      break;

      case STS_MKEY_BREAK: // [3-2]->
        if (c == 0x7C) {
          state = STS_MKEY_BREAK_SC2; scIndex=2; continue; // ->[3-2-2]
        } else {
          code = findcode(c);
          if (code) {
            // [3-2]->[3-2-1] BREAK+マルチバイト2バイト目(END)
            code |= BREAK_CODE;
            goto DONE; 
          } else {
            goto STS_ERROR; // -> ERROR
          }          
        }
        break;

      case STS_MKEY_BREAK_SC2: // [3-2-2]->
        scIndex++;
        if (scIndex >= sizeof(prnScrncode2)) {
          goto STS_ERROR; // -> ERROR
        }
        if (c == prnScrncode2[scIndex]) {
          if (scIndex == sizeof(prnScrncode2)-1) {
            // ->[3-2-2-1-1-1](END)
            code = PS2_PrintScreen | BREAK_CODE; // BREAK+PrintScreen
            goto DONE;            
          } else {
            continue;
          }
        }
        break;
      
      case STS_MKEY_SC2:  // [3-3]->
        if ( c == 0xe0 ) {
          state  = STS_MKEY_SC3;
          continue;
        }
        break;
      
      case STS_MKEY_SC3:
    	switch(c) {
    		case 0x70:code = PS2_Insert;      goto DONE;break; // [INS]
    		case 0x71:code = PS2_Delete;      goto DONE;break; // [DEL]
    	    case 0x6B:code = PS2_L_Arrow;     goto DONE;break; // [←]
    	    case 0x6C:code = PS2_Home;        goto DONE;break; // [HOME]
    	    case 0x69:code = PS2_End;         goto DONE;break; // [END]
    	    case 0x75:code = PS2_Up_Arrow;    goto DONE;break; // [↑]
    	    case 0x72:code = PS2_Down_Arrow;  goto DONE;break; // [↓]
    	    case 0x7d:code = PS2_PageUp;      goto DONE;break; // [PageUp]
    	    case 0x7a:code = PS2_PageDown;    goto DONE;break; // [PageDown]
    	    case 0x74:code = PS2_R_Arrow;     goto DONE;break; // [→]
    	    case 0x7C:code = PS2_PrintScreen; goto DONE;break; // [PrintScreen]
    	    default:  goto STS_ERROR;break;  // -> ERROR
    	}
  	
    	break;
    	
      case STS_MKEY_PS:  // [4]->
        scIndex++;
        if (scIndex >= sizeof(pausescode)) {
              goto STS_ERROR; // -> ERROR
        }
        if (c == pausescode[scIndex]) {
          if (scIndex == sizeof(pausescode)-1) {
            // ->[4-1-1-1-1-1-1-1](END)
            code = PS2_Pause; // Pause key
            goto DONE;            
          } else {
            continue;
          }
        } else {
            goto STS_ERROR; // -> ERROR
        }
        break;

      default:
        goto STS_ERROR; // -> ERROR
        break;     
     }
  }
  NEXT:
    return KEY_NONE;
  STS_ERROR:
    code = KEY_ERROR;
  DONE:
    state = STS_SYOKI;
    scIndex = 0;
  return code; 
}

//
// 入力キー情報の取得(CapsLock、NumLock、ScrollLockを考慮)
// 仕様 
//  ・入力したキーに対応するASCIIコード文字またはキーコードと付随する情報を返す。
//    => keyEvent構造体(2バイト)
//    (ASCIIコードを持たないキーはキーコード、持つ場合はASCIIコード)
//    - 下位8ビット ASCIIコードまたはキーコード
//    - 上位8ビットには下記の情報がセットされる
//       B00000001 : Make/Break種別  0:Make(押した)　1:Break(離した)
//       B00000010 : ASCIIコード/キーコード種別  0:ASCII 1:キーコード
//       B00000100 : Shift有無(CapsLock考慮) 0:なし 1:あり
//       B00001000 : Ctrl有無 0:なし 1:あり
//       B00010000 : Alt有無  0:なし 1:あり
//       B00100000 : GUIあり  0:なし 1:あり
//
//    - エラーまたは入力なしの場合、下位8ビットに以下の値を設定する
//     0x00で入力なし、0xFFでエラー
//
// 戻り値: 入力情報
//

keyEvent TKeyboard::read() {
  //static uint16_t sts_state     = 0;           // キーボード状態
  static keyinfo  sts_state = {.value = 0};
  static uint8_t  sts_numlock   = LOCK_Start;  // NumLock状態
  static uint8_t  sts_CapsLock  = LOCK_Start;  // CapsLock状態
  static uint8_t  sts_ScrolLock = LOCK_Start;  // ScrollLock状態

  keyinfo c;
  uint16_t code;
  uint16_t bk;

  // キーコードの取得
  c.value = 0;
  code = scanToKeycode();
  bk = code & BREAK_CODE;
  code &= 0xff;
  
  if (code == 0 || code == 0xff)
 	  goto DONE; // キー入力なし、またはエラー

  // 通常キー
//  if (code >= PS2_ESC && code <= PS2_Z) {
  if (code >= PS2_Space && code <= PS2_Z) {	
     if (code >= PS2_A && code <= PS2_Z)  // A-ZのCapsLockキー状態に影響するキーの場合の処理
        c.value = key_ascii[code-PS2_Space][((sts_CapsLock&1)&&sts_state.kevt.SHIFT)||(!(sts_CapsLock&1)&&!sts_state.kevt.SHIFT)?0:1];
      else 
        c.value = key_ascii[code-PS2_Space][sts_state.kevt.SHIFT?1:0];
     goto DONE;
     
  } else if (code >= PS2_PAD_Equal && code <= PS2_PAD_Slash) {
   // テンキー
   if ( (sts_numlock & 1) &&  !sts_state.kevt.SHIFT ) {
      // NumLock有効でShiftが押させていない場合
      c.value = tenkey[code-PS2_PAD_Equal][0];      
      //Serial.println("[DEBUG:NumLock]");
   } else {
      c.value = tenkey[code-PS2_PAD_Equal][1];
      if (tenkey[code-PS2_PAD_Equal][2]) c.value |= KEY_CODE;
  	}
  	goto DONE;
    
  } else if (code >=PS2_L_Alt && code <= PS2_CapsLock) {
    // 入力制御キー

    // 操作前のLockキーの状態を保存
    uint8_t prv_numlock = sts_numlock & 1;
    uint8_t prv_CapsLock = sts_CapsLock & 1;
    uint8_t prv_ScrolLock = sts_ScrolLock & 1;
  
    switch(code) {
  	  case PS2_L_Shift:  // Shiftキー
  	  case PS2_R_Shift:  sts_state.kevt.SHIFT = bk?0:1; break;
  	  case PS2_L_Ctrl:   // Ctrlキー
  	  case PS2_R_Ctrl:   sts_state.kevt.CTRL = bk?0:1;  break;
  	  case PS2_L_Alt:    // Altキー
  	  case PS2_R_Alt:    sts_state.kevt.ALT = bk?0:1; break; 	 
      case PS2_L_GUI:    // Windowsキー
      case PS2_R_GUI:    sts_state.kevt.GUI = bk ? 0:1;  break; 
  	  case PS2_CapsLock: // CapsLockキー(トグル動作)
    		switch (sts_CapsLock) {
          case LOCK_Start:	  if (!bk) sts_CapsLock = LOCK_ON_Make; break;  // 初期
  		    case LOCK_ON_Make:  if (bk)  sts_CapsLock = LOCK_ON_Break; break; // LOCK ON キーを押したままの状態
  		    case LOCK_ON_Break: if (!bk) sts_CapsLock = LOCK_OFF_Make;break;  // LOCK ON キーを離した状態
  		    case LOCK_OFF_Make: if (bk)  sts_CapsLock = LOCK_Start;break;     // LOCK OFF キーを押したままの状態
  		    default: sts_CapsLock = LOCK_Start; break;
  		  }
  		  break; 		
  	  case PS2_ScrollLock: // ScrollLockキー(トグル動作)
        switch (sts_ScrolLock) {
          case LOCK_Start:    if (!bk) sts_ScrolLock = LOCK_ON_Make; break;  // 初期
          case LOCK_ON_Make:  if (bk)  sts_ScrolLock = LOCK_ON_Break; break; // LOCK ON キーを押したままの状態
          case LOCK_ON_Break: if (!bk) sts_ScrolLock = LOCK_OFF_Make;break;  // LOCK ON キーを離した状態
          case LOCK_OFF_Make: if (bk)  sts_ScrolLock = LOCK_Start;break;     // LOCK OFF キーを押したままの状態
          default: sts_ScrolLock = LOCK_Start; break;
        }
        break; 
      case PS2_NumLock: // NumLockキー(トグル動作)
        switch (sts_numlock) {
          case LOCK_Start:    if (!bk) sts_numlock  = LOCK_ON_Make; break;  // 初期
          case LOCK_ON_Make:  if (bk)  sts_numlock  = LOCK_ON_Break; break; // LOCK ON キーを押したままの状態
          case LOCK_ON_Break: if (!bk) sts_numlock  = LOCK_OFF_Make;break;  // LOCK ON キーを離した状態
          case LOCK_OFF_Make: if (bk)  sts_numlock  = LOCK_Start;break;     // LOCK OFF キーを押したままの状態
          default: sts_numlock = LOCK_Start; break;
        }
        break; 
  	  default: goto ERROR; break;
  	}
  	c.value = KEY_NONE; // 入力制御キーは入力なしとする
  
    // LEDの制御(各Lockキーの状態が変わったらLEDの制御を行う)
    if ((prv_numlock != (sts_numlock & 1)) || (prv_CapsLock  != (sts_CapsLock & 1)) || (prv_ScrolLock != (sts_ScrolLock & 1)))
       ctrl_LED(sts_CapsLock & 1, sts_numlock & 1, sts_ScrolLock & 1);
    goto DONE;

  } else if (code == PS2_HanZen && _flgUS) {
    if (sts_state.kevt.SHIFT)
      c.value = '`';
    else
      c.value = '~';
    goto DONE; 
  } else {
    // その他の文字(キーコードを文字コードとする)
    c.value = code|KEY_CODE;
    goto DONE;
  }

ERROR:
  c.value = KEY_ERROR;
  
DONE: 
  c.value |= sts_state.value|bk ;
  return c.kevt;
}

// キーボードLED点灯設定
// 引数
//  swCaps : CapsLock   LED制御 0:off 1:on
//  swNum  : NumLock    LED制御 0:off 1:on
//  swScrol: ScrollLock LED制御 0:off 1:on
// 戻り値
//  0:正常 1:異常
//
uint8_t TKeyboard::ctrl_LED(uint8_t swCaps, uint8_t swNum, uint8_t swScrol) {

  if(!_flgLED)
    return 0;

  uint8_t c=0,err,tmp;
  //pb.disableInterrupts();
    
  if(swCaps)  c|=0x04; // CapsLock LED
  if(swNum)   c|=0x02; // NumLock LED
  if(swScrol) c|=0x01; // ScrollLock LED

  if (err = pb.send(0xed)) goto ERROR;
  if (err = pb.rcev(&tmp)) goto ERROR;
  if (tmp != 0xFA) {
    //Serial.println("Send Error 1");
    goto ERROR;
  }
  if (err = pb.send(c)) goto ERROR;   
  if (err = pb.rcev(&tmp)) goto ERROR;
  if (tmp != 0xFA) {
    //Serial.println("Send Error 2");
    goto ERROR;
  }
  goto DONE;
  
ERROR:
  // キーボードの初期化による復旧
  init();
DONE:
  return err;  
}
