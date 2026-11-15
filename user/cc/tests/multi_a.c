extern int counter;
extern int bump(int by);

int main(void)
{
    bump(2);
    bump(3);
    return counter == 5 ? 0 : 1;
}
