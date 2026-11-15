#include <stdio.h>

long long mix(long long a, long long b, int n)
{
    return (a * b) / 3 + (a % b) + (a << n) + (b >> n);
}

int main(void)
{
    unsigned long long u = 123456789012ULL;
    long long r = mix(1000000007LL, 998244353LL, 5);
    printf("%d %d\n", (int)(r % 1000), (int)(u % 97));
    return 0;
}
