native strlen(const str[]);

new text[44] = ''the quick brown fox jumps over the lazy dog'';

scan(rounds)
{
    new hits = 0;
    new joined = 0;
    new n = strlen(text);
    for (new r = 0; r < rounds; r++)
    {
        for (new i = 0; i < n; i++)
        {
            if (text[i] == 111)
            {
                hits++;
            }
        }
        new pair[3];
        pair[0] = text[r % n];
        pair[1] = text[(r + 1) % n];
        pair[2] = 0;
        joined += strlen(pair);
    }
    return hits + joined;
}

main()
{
    return scan(20000);
}
