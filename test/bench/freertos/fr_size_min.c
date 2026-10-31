/* 包一層，讓兩個環境（最小設定 / 預設設定）各自產生獨立的 .o，避免 PlatformIO 的 build 資料夾互相覆蓋。 */
#include "fr_size_main.c"
