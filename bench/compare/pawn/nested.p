/* The cnor benchmark workload `nested` in Pawn: 1000 x 1000 nested for loops. */

nested(n)
{
    new s = 0;
    for (new i = 0; i < n; i++)
    {
        for (new j = 0; j < n; j++)
        {
            s += (i * j) % 7;
        }
    }
    return s;
}

main()
{
    return nested(1000);
}
