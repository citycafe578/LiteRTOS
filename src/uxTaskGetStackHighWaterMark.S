.global prvCountUntouchedStack
.text

; r25:r24 = stackBase 指標
; r23:r22 = stackDepth
; r20     = LITERTOS_STACK_FILL
; 回傳：r25:r24 = untouched 數量
prvCountUntouchedStack:
    movw  r30, r24          ; Z = stackBase
    movw  r26, r22          ; X = stackDepth
    clr   r24
    clr   r25
1:
    sbiw  r26, 1            ; 剩餘長度減 1
    brmi  2f                ; 讀完了就跳出
    ld    r18, Z+           ; 讀取記憶體，Z 自動遞增
    cp    r18, r20          ; 比對是否為填滿數值
    brne  2f                ; 遇到已使用的記憶體就跳出
    adiw  r24, 1            ; 計數器加 1
    rjmp  1b
2:
    ret