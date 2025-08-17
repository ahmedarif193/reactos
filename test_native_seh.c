/* Test file to verify native SEH works on AMD64 MinGW */
#include <windows.h>
#include <stdio.h>

int test_native_seh(void)
{
    printf("Testing native SEH on AMD64 MinGW...\n");
    
    __try {
        printf("In try block\n");
        /* Force an exception */
        int *p = NULL;
        *p = 42;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        printf("Exception caught with native SEH!\n");
        return 1;
    }
    
    printf("Should not reach here\n");
    return 0;
}

int main(void)
{
    return test_native_seh();
}