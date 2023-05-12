#/bin/bash

diff <(./cxy dev ../test/lang/literals.cxy --print-ast --no-color --clean) <(cat ../test/lang/literals.expected) -c