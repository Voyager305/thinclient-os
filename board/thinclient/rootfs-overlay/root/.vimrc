" Минимальный конфиг: vim собран без runtime-файлов (экономия ~25 МБ),
" без этого файла он при старте ругается E1187 на отсутствующий defaults.vim
set nocompatible
set backspace=indent,eol,start
set ruler
set showcmd
set nowrap
" ни syntax on, ни syntax off тут писать нельзя: обе команды сорсят
" runtime-файлы, которых в образе нет (vim собран без runtime)
