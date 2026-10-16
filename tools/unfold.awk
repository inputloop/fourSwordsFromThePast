# unfold.awk: print the body of every "#+begin_src sh :tangle check" block of an Org file, in order.
# The first of the recipe's two readers (the second is tools/unfold.py); the two must agree byte for byte.
BEGIN { inb = 0 }
/^[ \t]*#\+begin_src[ \t]+sh[ \t]+.*:tangle[ \t]+check([ \t]|$)/ { inb = 1; next }
inb && /^[ \t]*#\+end_src/ { inb = 0; next }
inb { print }
