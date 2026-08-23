#!/usr/bin/env bash
#
# The machine-independent memory layer must stay machine-independent.
#
# A boundary that is only ever cleaned up is not a boundary -- it is a
# clean-up, and it rots the first time someone needs a page table entry in a
# hurry. mm/ had thirty-seven such references when this check was written, and
# the arm64 port was therefore not "write a second pmap" but "rewrite mm/".
#
# So the rule is checked rather than remembered.
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# Identifiers that only a particular machine could mean, and the headers that
# would let mm/ reach them. pmap.h is exempt: naming what it hides is its job.
BANNED_IDENT='PTE_[A-Z_]+|pml4|paging_[a-z_0-9]+|__asm__|rdmsr|wrmsr|cr3'
BANNED_INCLUDE='arch/paging\.h|arch/x86_64/|arch/arm64/|arch/riscv64/'

fail=0
for f in mm/*.c mm/*.h; do
  [ "$f" = "mm/pmap.h" ] && continue

  if hits=$(grep -nE "$BANNED_IDENT" "$f" 2>/dev/null); then
    echo "[mm-boundary] $f names something only one machine could mean:"
    echo "$hits" | sed 's/^/    /'
    fail=1
  fi
  if hits=$(grep -nE "#include.*($BANNED_INCLUDE)" "$f" 2>/dev/null); then
    echo "[mm-boundary] $f includes a machine-dependent header:"
    echo "$hits" | sed 's/^/    /'
    fail=1
  fi
done

if [ $fail -ne 0 ]; then
  echo
  echo "[mm-boundary] Route it through mm/pmap.h, or add it to that contract."
  exit 1
fi

echo "[mm-boundary] mm/ is free of machine-dependent references"
