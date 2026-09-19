
int strlen(const char s[])
{
    int i = 0;
    while (s[i] != '\0')
    {
        i++;
    }
    return i;
}

void reverse(char s[])
{
    int size = strlen(s);
    for (int i=0; i<size/2; ++i)
    {
        char temp = s[i];
        s[i] = s[size-i-1];
        s[size-i-1] = temp;
    }
}