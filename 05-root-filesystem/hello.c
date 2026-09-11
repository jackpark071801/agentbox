#include <stdio.h>
#include <unistd.h>

int main(void)
{
    printf("Hello from inside the root filesystem!\n");
    printf("PID: %d\n", getpid());
    return 0;
}
