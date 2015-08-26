" chr.vim
" Gregory J. Duck
" SMCHR syntax file
"
" INSTRUCTIONS:
"   1) Put this file in your ~/.vim/ directory.
"   2) Add the following lines to your .vimrc:
"       au BufRead,BufNewFile *.chr set filetype=chr
"       au! Syntax chr source ~/.vim/chr.vim

if exists("b:current_syntax")
    finish
endif

syn match   smchrLineComment        "\/\/.*"
syn region  smchrComment            start="/\*" end="\*/"
syn region  smchrString             start=+"+  skip=+\\\\\|\\"+  end=+"\|$+
syn keyword smchrTypeDecl           type priority low medium high
syn keyword smchrTypeInst           var atom num int bool nil str any of

hi link smchrLineComment            Comment
hi link smchrComment                Comment
hi link smchrTypeDecl               Special
hi link smchrTypeInst               Type
hi link smchrString                 String

