#include <stdio.h>

/*
    STATE macro
    - Declares a variable
    - Declares a setter function
*/
#define STATE(type, var, setter, initial) \
    type var = initial;                  \
    void setter(type value) {            \
        var = value;                     \
    }

/* Define states */
STATE(int, count, setCount, 0)
STATE(float, temperature, setTemperature, 25.5f)
STATE(char, grade, setGrade, 'A')

int main() {
    printf("Initial values:\n");
    printf("count = %d\n", count);
    printf("temperature = %.2f\n", temperature);
    printf("grade = %c\n\n", grade);

    /* Update state */
    setCount(10);
    setTemperature(32.8f);
    setGrade('B');

    printf("After updates:\n");
    printf("count = %d\n", count);
    printf("temperature = %.2f\n", temperature);
    printf("grade = %c\n", grade);

    return 0;
}
