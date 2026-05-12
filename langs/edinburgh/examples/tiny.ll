define i64 @tiny(i64 %a, i64 %b, i64 %c) {
entry:
  %mul = mul i64 %a, %b
  %add = add i64 %mul, %c
  %xor = xor i64 %add, %a
  ret i64 %xor
}
