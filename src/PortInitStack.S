.global LiteRTOS_portInitStack
.text

; 參數慣例 (AVR-GCC)：
; r25:r24 = top (堆疊頂端指標)
; r23:r22 = entry (函式指標，r23 高位、r22 低位)
; 回傳值：r25:r24 = 新的 top
LiteRTOS_portInitStack:
    movw  r30, r24          ; Z = top（指向最後一個可用位元組）
    adiw  r30, 1            ; st -Z 是先減再寫，所以先 +1，第一個位元組才會寫在 top 本身

    st    -Z, r22           ; 寫入 entry 低位元組
    st    -Z, r23           ; 寫入 entry 高位元組

    ldi   r18, 18           ; 迴圈次數：18 個暫存器
    clr   r19               ; 清零
1:
    st    -Z, r19           ; 寫入 0
    dec   r18
    brne  1b

    ldi   r18, 0x80         ; SREG (I 旗標 = 1)
    st    -Z, r18

    sbiw  r30, 1            ; 回傳指向 SREG 下方的空位（和 C 版相同）
    movw  r24, r30          ; 回傳值放進 r25:r24
    ret