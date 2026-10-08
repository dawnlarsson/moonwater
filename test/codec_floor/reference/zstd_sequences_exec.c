/* Semantic reference for zstd_sequences_exec: the copies of a block's records
   a byte at a time. A record is a literal length, a match length and an
   offset: that many literal bytes, then that many bytes from offset bytes
   back, which may be bytes this sequence has just written (an offset under
   the length repeats). Nothing is checked, as in the routine; the caller
   has proved the bounds. test/checks.c includes this file as its model and
   compares the bytes the records make. */
static fn zstd_sequences_exec_reference(zstd_seq_exec_job address_to j)
{
        p8 address_to out = j->out;
        p8 address_to lits = j->lits;

        for (positive i = 0; i < j->nseq; i++)
        {
                p32 const address_to r = j->recs + 3 * i;

                for (p32 k = 0; k < r[0]; k++)
                        *out++ = *lits++;
                for (p32 k = 0; k < r[1]; k++, out++)
                        *out = out[-(bipolar)r[2]];
        }
        j->out = out;
        j->lits = lits;
}
