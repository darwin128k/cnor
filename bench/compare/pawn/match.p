/* The cnor benchmark workload `match` in Pawn: 1M iterations of a 7-arm match. */

classify(n)
{
    new s = 0;
    for (new i = 0; i < n; i++)
    {
        switch (i % 8)
        {
            case 0: s += 1;
            case 1: s += 3;
            case 2: s -= 2;
            case 3: s += 5;
            case 4: s *= 1;
            case 5: s += 7;
            default: s += 0;
        }
    }
    return s;
}

main()
{
    return classify(1000000);
}
