#include <stdio.h>

double scale(double x, int n)
{
    return x * n + 1.5;
}

int main(void)
{
    double d = scale(2.25, 4);
    float f = (float)d / 3;
    printf("%d %d %f\n", (int)d, (int)(f * 100), d);
    return d > 10.0 && f < 4.0 ? 0 : 1;
}
