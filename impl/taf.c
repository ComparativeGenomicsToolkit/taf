#include "taf.h"

/*
 * Returns non-zero if the tokens list contains the coordinate marker ':'
 */
static bool has_coordinates(stList *tokens, int64_t *j) {
    for(*j=0; *j<stList_length(tokens); (*j)++) {
        if(strcmp(stList_get(tokens, *j), ";") == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Parse the sequence_name, start, strand and sequence_length fields for a row
 */
void parse_coordinates(Alignment_Row *row, int64_t *j, stList *tokens) {
   row->sequence_name = stString_copy(stList_get(tokens, (*j)++));
   row->start = atol(stList_get(tokens, (*j)++));
   assert(strcmp(stList_get(tokens, (*j)), "+") == 0 || strcmp(stList_get(tokens, (*j)), "-") == 0);
   row->strand = strcmp(stList_get(tokens, (*j)++), "+") == 0;
   row->sequence_length = atol(stList_get(tokens, (*j)++));
}

/*
 * Make the block being parsed by copying the previous block and then editing it with the
 * list of coordinate changes.
 */
static Alignment *parse_coordinates_and_establish_block(Alignment *p_block, stList *tokens) {
    // Make a new block
    Alignment *alignment = st_calloc(1, sizeof(Alignment));

    // Copy the rows of the previous block
    Alignment_Row **p_row = &(alignment->row), *l_row = p_block == NULL ? NULL : p_block->row;
    while(l_row != NULL) {
        Alignment_Row *row = st_calloc(1, sizeof(Alignment_Row));
        alignment->row_number++; // Increment the row number
        // Copy the relevant fields
        row->start = l_row->start + l_row->length;
        row->sequence_name = stString_copy(l_row->sequence_name);
        row->sequence_length = l_row->sequence_length;
        row->strand = l_row->strand;
        // Link up the previous and current rows
        *p_row = row;
        p_row = &(row->n_row);
        // Link corresponding left and right rows
        l_row->r_row = row;
        row->l_row = l_row;
        // Get the next row to copy
        l_row = l_row->n_row;
    }
    assert(p_block == NULL || alignment->row_number == p_block->row_number);

    // Now parse the tokens to edit the rows
    int64_t j;
    has_coordinates(tokens, &j); j++; // Get 1+ the coordinate of the ';' token
    while(j < stList_length(tokens)) { // Iterate through the tokens
        char *op_type = stList_get(tokens, j++); // This is the operation
        assert(strlen(op_type) == 1); // Must be a single character in length
        int64_t row_index = atol(stList_get(tokens, j++)); // Get the index of the affected row
        int64_t i=0;
        Alignment_Row **row = &(alignment->row); // Get the pointer to the pointer to the row being modded
        while(i++ < row_index) {
            assert(*row != NULL);
            row = &((*row)->n_row);
        }
        if(op_type[0] == 'i') { // Is inserting a row
            alignment->row_number++;
            Alignment_Row *new_row = st_calloc(1, sizeof(Alignment_Row)); // Make the new row
            // Connect it up, putting the new row immediately before the old one
            new_row->n_row = *row;
            *row = new_row;
            // Fill it out
            parse_coordinates(new_row, &j, tokens);
        } else if(op_type[0] == 's') { // Is substituting a row
            free((*row)->sequence_name); // clean up
            parse_coordinates(*row, &j, tokens);
        } else if(op_type[0] == 'd') { // Is deleting a row
            // Remove the row from the list of rows
            alignment->row_number--;
            Alignment_Row *r = *row;
            *row = r->n_row;
            // Fix left pointer of row in previous block
            assert(r->l_row != NULL);
            r->l_row->r_row = NULL;
            // Now delete the row
            r->n_row = NULL;
            Alignment_Row_destruct(r);
        } else if(op_type[0] == 'g') { // Is making a gap without the sequence specified
            int64_t gap_length = atol(stList_get(tokens, j++)); // Get the index of the affected row
            (*row)->start += gap_length;
        } else { // Is making a gap with the sequence specified
            assert(op_type[0] == 'G');
            (*row)->left_gap_sequence = stString_copy(stList_get(tokens, j++));
            (*row)->start += strlen((*row)->left_gap_sequence);
        }
    }

    return alignment;
}

/*
 * Parse the base alignment for the column.
 */
char *get_bases(int64_t column_length, stList *tokens, bool run_length_encode_bases) {
    if(run_length_encode_bases) { // Case the bases are encoded using run length encoding
        char *column = st_calloc(column_length, sizeof(char));
        int64_t i=0, j=0;
        while(j < column_length) {
            assert(i < stList_length(tokens));
            char *base_token = stList_get(tokens, i++);
            assert(strlen(base_token) == 1); // The base must be a single character
            int64_t k = atol(stList_get(tokens, i++));
            assert(k > 0); // Each count must be greater than zero
            while(k-- > 0) {
                column[j++] = base_token[0];
            }
            assert(j <= column_length);
        }
        assert(j == column_length);
        return column;
    }
    // Otherwise column is just a string of bases without whitespace
    char *column = stString_copy(stList_get(tokens, 0));
    assert(strlen(column) == column_length); // Must be a contiguous run of bases equal in length to the number of rows
    return column;
}

/*
 * Gets the first non-empty line, return NULL if reaches end of file
 */
static stList *get_first_line(LI *li) {
    stList *tokens = NULL;
    while(1) { // Loop to find the tokens for the first line
        // Read column with coordinates first, using previous alignment block and the coordinates
        // to create the set of rows
        char *line = LI_get_next_line(li);

        if (line == NULL) { // At end of file
            return NULL;
        }

        // Tokenize the line
        tokens = stString_split(line);
        free(line);

        if (stList_length(tokens) == 0) { // Is a white space only line, just ignore it
            stList_destruct(tokens);
            tokens = NULL;
            continue;
        }
        else { // We have the first line of the block
            break;
        }
    }
    return tokens;
}

Alignment *taf_read_block(Alignment *p_block, bool run_length_encode_bases, LI *li) {
    stList *tokens = get_first_line(li); // Get the first non-empty line

    if(tokens == NULL) { // If there are no more lines to be had return NULL
        return NULL;
    }

    // Find the coordinates
    Alignment *block = parse_coordinates_and_establish_block(p_block, tokens);

    // Now add in all subsequent columns until we get one with coordinates, which we push back
    stList *alignment_columns = stList_construct3(0, free);
    stList_append(alignment_columns, get_bases(block->row_number, tokens, run_length_encode_bases));
    stList_destruct(tokens); // Clean up the first row
    while(1) {
        char *line = LI_peek_at_next_line(li);

        if(line == NULL) { // We have reached the end of the file
            break;
        }

        // tokenize the line
        tokens = stString_split(line);

        if(stList_length(tokens) == 0) { // Is a white space only line, just ignore it
            free(LI_get_next_line(li)); // pull the line and clean it up
            stList_destruct(tokens); // clean up
            continue;
        }

        int64_t i;
        if(has_coordinates(tokens, &i)) { // If it has coordinates we have reached the end of the block, so break
            // and don't pull the line
            stList_destruct(tokens); // clean up
            break;
        }

        // Add the bases from the line as a column to the alignment
        stList_append(alignment_columns, get_bases(block->row_number, tokens, run_length_encode_bases));

        free(LI_get_next_line(li)); // pull the line and clean up the memory for the line
        stList_destruct(tokens); // clean up the tokens
    }

    //Now parse the actual alignments into the rows
    Alignment_Row *row = block->row;
    int64_t j = 0, k = stList_length(alignment_columns);
    char bases[k+1];
    bases[k] = '\0';
    while(row != NULL) {
        int64_t length = 0;
        for(int64_t i=0; i<k; i++) {
            char *column = stList_get(alignment_columns, i);
            bases[i] = column[j];
            if(bases[i] != '-') {
                length++;
            }
        }
        row->bases = stString_copy(bases);
        row->length = length;
        row = row->n_row; j++;
    }
    assert(j == block->row_number);

    // Clean up
    stList_destruct(alignment_columns);

    return block;
}

stList *taf_read_header(LI *li) {
    stList *tokens = get_first_line(li);
    assert(tokens != NULL); // There has to be a valid header line
    stList *tags = parse_header(tokens, "#", ":");
    stList_destruct(tokens);

    return tags;
}

void write_column(Alignment_Row *row, int64_t column, FILE *fh, bool run_length_encode_bases) {
    char base = '\0';
    int64_t base_count = 0;
    while(row != NULL) {
        if(row->bases[column] == base) {
            base_count++;
        }
        else {
            if(base != '\0') {
                if(run_length_encode_bases) {
                    fprintf(fh, "%c %" PRIi64 " ", base, base_count);
                }
                else {
                    for (int64_t i = 0; i < base_count; i++) {
                        fprintf(fh, "%c", base);
                    }
                }
            }
            base = row->bases[column];
            base_count = 1;
        }
        row = row->n_row;
    }
    if(base != '\0') {
        if(run_length_encode_bases) {
            fprintf(fh, "%c %" PRIi64 " ", base, base_count);
        }
        else {
            for (int64_t i = 0; i < base_count; i++) {
                fprintf(fh, "%c", base);
            }
        }
    }
}

void write_coordinates(Alignment_Row *p_row, Alignment_Row *row, FILE *fh) {
    int64_t i = 0;
    fprintf(fh, " ;");
    while(p_row != NULL) { // Write any row deletions
        if(p_row->r_row == NULL) { // if the row is deleted
            fprintf(fh, " d %" PRIi64 "", i);
        }
        else { // Only update the index is the row is not deleted
            i++;
        }
        p_row = p_row->n_row;
    }
    i = 0;
    while(row != NULL) { // Now write the new rows
        if(row->l_row == NULL) { // if the row is inserted
            fprintf(fh, " i %" PRIi64 " %s %" PRIi64 " %c %" PRIi64 "",
                    i, row->sequence_name, row->start, row->strand ? '+' : '-', row->sequence_length);
        }
        else {
            if(strcmp(row->l_row->sequence_name, row->sequence_name) == 0 &&
                row->l_row->strand == row->strand &&
                row->l_row->start + row->l_row->length <= row->start) {
                if(row->l_row->start + row->l_row->length < row->start) { // if there is an indel
                    fprintf(fh, " g %" PRIi64 " %" PRIi64 "",
                            i, row->start - (row->l_row->start + row->l_row->length));
                }
            }
            else { // Substitute one row for another
                fprintf(fh, " s %" PRIi64 " %s %" PRIi64 " %c %" PRIi64 "",
                        i, row->sequence_name, row->start, row->strand ? '+' : '-', row->sequence_length);
            }
        }
        row = row->n_row; i++;
    }
}

void taf_write_block(Alignment *p_alignment, Alignment *alignment, bool run_length_encode_bases, FILE *fh) {
    Alignment_Row *row = alignment->row;
    if(row != NULL) {
        int64_t column_no = strlen(row->bases);
        assert(column_no > 0);
        write_column(row, 0, fh, run_length_encode_bases);
        write_coordinates(p_alignment != NULL ? p_alignment->row : NULL, row, fh);
        fprintf(fh, "\n");
        for(int64_t i=1; i<column_no; i++) {
            write_column(row, i, fh, run_length_encode_bases);
            fprintf(fh, "\n");
        }
    }
}

void taf_write_header(stList *tags, FILE *fh) {
    assert(stList_length(tags) % 2 == 0); // list must be a sequence of alternative key:value pairs
    fprintf(fh, "#");
    for(int64_t i=0; i<stList_length(tags); i+=2) {
        fprintf(fh, " %s:%s", (char *)stList_get(tags, i), (char *)stList_get(tags, i+1));
    }
    fprintf(fh, "\n");
}

