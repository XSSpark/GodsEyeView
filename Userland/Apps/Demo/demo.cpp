#include "LibC/stdio.hpp"
#include "LibC/stdlib.hpp"
#include "LibC/unistd.hpp"

int main()
{
    int result;
    char buffer[22];

    result = open((char*)"welcome");
    read(result, buffer, 22);
    close(result);
    printf("%s", buffer);

    exit(0);

    while (1)
        ;
}
