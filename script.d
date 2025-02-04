BEGIN{printf("DTrace attached");}
 profile-997/pid == $target/{@stacks[tid, ustack(50, 500)] = count();}

*:*:method-entry
{
    jstack();
}


END{printf("DTrace stopped\n");
printf("<aggregated-data-tag>CPU Samples</data-start>\n"); printa(@stacks); printf("<aggregated-data-tag></data-end>\n");}
