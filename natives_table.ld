SECTIONS
{
    .native :
    {
        __start_natives = .;
        KEEP(*(SORT_BY_NAME(.native.*)));
        __stop_natives = .;
    } > FLASH
}