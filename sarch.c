#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define EXIT_MEMERR()       exit(5)

enum {ctable_size       = 256};
enum {char_code_len_max = 256};
enum {signature_size    = 4};
enum {charbuf_size      = 8};

enum {inbuf_size        = 8192};
enum {outbuf_size       = 8192};

enum {opt_compress      = 1 << 0};
enum {opt_extract       = 1 << 1};

enum {
    f_ok    = 0,
    f_eof   = 1,
    f_err   = 2,
    f_ovs   = 3
};

struct node {
    int ch;
    unsigned int frequency;
    char *code;
    struct node *left;
    struct node *right;
};

struct fileinfo {
    struct node *tree;
    struct node **stree;
    struct node *leafs[ctable_size];
    unsigned int ctable[ctable_size];
    unsigned long ufsize;
    unsigned long cfsize;
    int chars_count;
};

struct codebuf {
    unsigned char byte;
    int count;
};

struct cmd_options {
    int options;
    char *input_fname;
    char *output_fname;
};

struct io_buffer {
    unsigned char *buffer;
    int posl;
    int posf;
};

struct file {
    FILE *fd;
    char *fname;
    struct io_buffer *ibuf;
    struct io_buffer *outbuf;
    int status;
};

const char msg_fwerr[] = "Writing to output file failed";
const char msg_merr[] = "Fatal: malloc error\n";
const char msg_cerr[] = "Compress file is failed\n";
const char msg_xerr[] = "Extract file is failed\n";
const char msg_infempt[] = "Input file is empty\n";

void memerr()
{
    fputs(msg_merr, stderr);
    EXIT_MEMERR();
}

int write_in_file(const void *buf, int size, FILE *fd)
{
    int bytes;
    bytes = fwrite(buf, 1, size, fd);
    if (bytes != size)
        return f_err;
    else
        return f_ok;
}

int read_from_file(void *buf, int size, FILE *fd)
{
    int bytes;
    bytes = fread(buf, 1, size, fd);
    if (bytes != size) {
        if (feof(fd))
            return f_eof;
        else
            return f_err;
    }
    else {
        return f_ok;
    }
}

int write_buffer(struct file *f)
{
    int n;

    if (f->outbuf->posl == 0) {
        f->status = f_ok;
        return 0;
    }

    n = fwrite(f->outbuf->buffer, 1, f->outbuf->posl, f->fd);
    if (n != f->outbuf->posl)
        f->status = f_err;
    else 
        f->status = f_ok;

    f->outbuf->posl -= n;
    return n;
}

int file_buffer_push(struct file *f, const void *p, size_t size)
{
    size_t n;
    while (size) {
        n = outbuf_size - f->outbuf->posl;
        if (n == 0) {
            write_buffer(f);
            if (f->status)
                return 1;
        }

        if (size <= n) 
            n = size;

        memcpy(f->outbuf->buffer + f->outbuf->posl, p, n);
        f->outbuf->posl += n;
        size -=n;
        p = (unsigned char*)p + n;
    }
    return 0;
}

int read_in_buffer(struct file *f, int count)
{
    int n;

    n = fread(f->ibuf->buffer, 1, count, f->fd);
    if (n != count) {
        if (feof(f->fd))
            f->status = f_eof;
        else
            f->status = f_err;
    } else {
        f->status = f_ok;
    }

    f->ibuf->posl = n;
    f->ibuf->posf = 0;
    return n;
}

int file_buffer_pop(struct file *f, void *p, size_t size)
{
    size_t n;
    int rb;

    while (size) {
        n = f->ibuf->posl - f->ibuf->posf;
        if (n == 0) {
            rb = read_in_buffer(f, inbuf_size);
            if (rb == 0)
                return 1;
        }

        if (size <=n)
            n = size;

        memcpy(p, f->ibuf->buffer + f->ibuf->posf, n);
        f->ibuf->posf += n;
        size -= n;
        p = (unsigned char*)p + n;
    }
    return 0;
}

void init_finfo(struct fileinfo *finfo)
{
    memset(finfo, 0, sizeof(struct fileinfo));
}

void analyze_input_file(struct file *f, unsigned int *table)
{
    do {
        int i;
        read_in_buffer(f, inbuf_size);
        for (i = 0; i < f->ibuf->posl; i++)
            table[f->ibuf->buffer[i]]++;
    } while (f->status != f_eof);
}

void readfile(FILE *fd, unsigned int *table)
{
    int c;
    while ((c = fgetc(fd)) != EOF)
        table[c]++;
}

void calc_chars_count(struct fileinfo *finfo)
{
    int i;

    for (i = 0; i < ctable_size; i++) {
        if (finfo->ctable[i])
            finfo->chars_count++;
    }
}

void calc_ufilesize(struct fileinfo *finfo)
{
    int i;
    for (i = 0; i < ctable_size; i++)
        finfo->ufsize += finfo->ctable[i];
}

void create_tree_leafs(struct fileinfo *finfo)
{
    int i;

    finfo->tree = calloc((finfo->chars_count * 2) - 1, sizeof(struct node));
    if (finfo->tree == NULL)
        memerr();
    finfo->stree = malloc(sizeof(struct node*) * finfo->chars_count);
    if (finfo->stree == NULL)
        memerr();
    for (i = 0; i < finfo->chars_count; i++)
        finfo->stree[i] = finfo->tree + i;
}

void fill_leafs(struct fileinfo *finfo)
{
    int i;
    struct node *pnode = finfo->tree;

    for (i = 0; i < ctable_size; i++) {
        if (finfo->ctable[i]) {
            finfo->leafs[i] = pnode;
            finfo->leafs[i]->ch = i;
            finfo->leafs[i]->frequency = finfo->ctable[i];
            pnode++;
        }
    }
}

void swap_node_ptrs(struct node **n1, struct node **n2)
{
    struct node *tmp = *n1;
    *n1 = *n2;
    *n2 = tmp;
}

void sort_tree(struct node **stree, int size)
{
    int flag;

    do {
        int i;

        flag = 0;
        for (i = 0; i < size - 1; i++) {
            if (stree[i]->frequency < stree[i+1]->frequency) {
                swap_node_ptrs(stree + i, stree + i + 1);
                flag = 1;
            }
        }
    } while (flag);
}

void create_node(struct node *tree, struct node **stree, int ind_t, int ind_l)
{
    (tree + ind_t)->ch = -1;
    (tree + ind_t)->frequency = stree[ind_l]->frequency +
        stree[ind_l-1]->frequency;
    (tree + ind_t)->right = stree[ind_l];
    (tree + ind_t)->left = stree[ind_l-1]; 
    stree[ind_l-1] = tree + ind_t;
}

void resize_stree(struct fileinfo *finfo, int newsize)
{
    struct node **ptr;
    ptr = realloc(finfo->stree, sizeof(struct node*) * newsize);
    if (ptr == NULL)
        memerr();
    finfo->stree = ptr;
}

void build_tree(struct fileinfo *finfo)
{
    int i;

    for (i = 0; i < finfo->chars_count - 1; i++) {
        sort_tree(finfo->stree, finfo->chars_count - i);
        create_node(finfo->tree, finfo->stree, finfo->chars_count + i,
                finfo->chars_count - 1 - i);
        resize_stree(finfo, finfo->chars_count - i - 1);
    }
}

void node_bit(struct node *node, char *str, int s_index)
{
    if (node->ch == -1) {
        str[s_index] = '0';
        node_bit(node->left, str, s_index + 1);
        str[s_index] = '1';
        node_bit(node->right, str, s_index + 1);
    } else {
        str[s_index] = '\0';
        node->code = malloc(s_index + 1);
        if (node->code == NULL)
            memerr();

        strcpy(node->code, str);
    }
}

void char_codes(struct node *root)
{
    char str[char_code_len_max];
    int s_index = 0;

    node_bit(root, str, s_index);
}

void print_ctable(struct node **leafs)
{
    int i;
    for (i = 0; i < ctable_size; i++) {
        if (leafs[i] != NULL)
            printf("Char '%c'\t(byte %d),\tfrequency:\t%u,\tcode:\t%s\n",
                    leafs[i]->ch, leafs[i]->ch, leafs[i]->frequency,
                    leafs[i]->code);
    }
}

void printinfo(struct fileinfo *finfo)
{
    print_ctable(finfo->leafs);
    printf("Chars in file: %d\n", finfo->chars_count);
    printf("Uncompressed size: %lu\n", finfo->ufsize);
    printf("Compressed size: %lu\n", finfo->cfsize);
}

int write_info(struct fileinfo *finfo, struct file *f)
{
    int res = 0;
    int i;

    /* write signature */
    res += file_buffer_push(f,".sar", signature_size);
    finfo->cfsize += 4;

    /* write uncompress size */
    res += file_buffer_push(f, &finfo->ufsize, sizeof(finfo->ufsize));
    finfo->cfsize += sizeof(finfo->ufsize);

    /* write chars count */
    res += file_buffer_push(f, &finfo->chars_count, 1);
    finfo->cfsize += 1;

    /* write chars table */
    for (i = 0; i < ctable_size; i++) {
        if (finfo->ctable[i]) {
            res += file_buffer_push(f, &i, 1);
            res += file_buffer_push(f, &finfo->ctable[i], 
                sizeof(finfo->ctable[i]));
        }
    }

    finfo->cfsize += finfo->chars_count + finfo->chars_count *
        sizeof(*(finfo->ctable));
    return res;
}

int write_code(struct file *f, struct codebuf *buffer, const char *code)
{
    int byte_written = 0;

    while (*code) {
        buffer->byte += *code - '0';
        buffer->count++;
        code++;
        if (buffer->count < 8)
            buffer->byte = buffer->byte << 1;
        if (buffer->count == 8) {
            int result;
            result = file_buffer_push(f, &buffer->byte, sizeof(buffer->byte));
            if (result)
                return -1;
            byte_written++;
            buffer->count = 0;
            buffer->byte = 0;
        }
    }
    return byte_written;
}

int write_data(struct file *fin, struct file *fout, struct fileinfo *finfo)
{
    int res;
    struct codebuf buffer;

    buffer.count = 0;
    buffer.byte = 0;
    fin->ibuf->posl = 0;
    res = fseek(fin->fd, 0, SEEK_SET);
    if (res) 
        return 1;

    do {
        int i;
        unsigned char c;

        read_in_buffer(fin, inbuf_size);
        for (i = 0; i < fin->ibuf->posl; i++) {
            c = fin->ibuf->buffer[i];
            res = write_code(fout, &buffer, finfo->leafs[c]->code);
            if (res == -1)
                return 1;
            else
                finfo->cfsize += res;
        }
    } while (fin->status != f_eof);
    /* while ((value = fgetc(infd)) != EOF) {  */
    /*     res = write_code(outfd, &buffer, finfo->leafs[value]->code); */
    /*     if (res == -1) */
    /*         return 1; */
    /*     else */
    /*         finfo->cfsize += res; */
    /* } */

    if (buffer.count) {
        buffer.byte = buffer.byte << (8 - buffer.count - 1);
        res = file_buffer_push(fout, &buffer.byte, sizeof(buffer.byte));
        if (res)
            return 1;
        finfo->cfsize++;
    }
    return 0;
}

int read_info(struct file *fin, struct fileinfo *finfo)
{
    char str[5] = {0};
    int i, result;

    file_buffer_pop(fin, str, signature_size);
    result = strcmp(str, ".sar");
    if (result) {
        fputs ("Incorrect file signature\n", stderr);
        return 1;
    }

    file_buffer_pop(fin, &(finfo->ufsize), sizeof(finfo->ufsize));
    file_buffer_pop(fin, &(finfo->chars_count), 1);
    if (finfo->chars_count == 0) {
        fputs ("Incorrect header or corrupted file\n", stderr);
        return 1;
    }
    for (i = 0; i < finfo->chars_count; i++) {
        int tmp = 0;
        file_buffer_pop(fin, &tmp, 1);
        file_buffer_pop(fin, &(finfo->ctable[tmp]), sizeof(finfo->ctable[tmp]));
    } 
    return 0;
}

int decode_byte(char byte, struct node **node, struct node *root, char *buf)
{
    int i, count;
    unsigned char mbit;

    count = 0;
    mbit = 1 << 7;
    for (i = 0; i < 8; i++) {
        if (byte & (mbit >> i))
            *node = (*node)->right;
        else
            *node = (*node)->left;

        if ((*node)->ch != -1) {
            *buf = (*node)->ch;
            count++;
            buf++;
            *node = root;
        }
    } 
    return count;
}

int decode_data(struct file *fin, struct file *fout, struct fileinfo *finfo)
{
    unsigned long written_bytes;
    char tmp;
    int rstat;
    struct node **node;
    struct node *root;
    char charbuf[charbuf_size];

    written_bytes = 0;
    node = finfo->stree;
    root = (*finfo->stree);
    while ((rstat = file_buffer_pop(fin, &tmp, 1)) == 0) {
        int wbytes;
        wbytes = decode_byte(tmp, node, root, charbuf);
        written_bytes += wbytes;
        if (written_bytes > finfo->ufsize) {
            wbytes = wbytes - (written_bytes - finfo->ufsize);
        }

        if (wbytes) {
            file_buffer_push(fout, charbuf, wbytes);
            if (fout->status)
                return 1;
        }
    }

    return 0;
}

int extract_file(struct file *fin, struct file *fout,  struct fileinfo *finfo)
{
    int result;
    result = read_info(fin, finfo);
    if (result)
        return 1;
    create_tree_leafs(finfo);
    fill_leafs(finfo);
    build_tree(finfo);
    char_codes(*(finfo->stree));
    result = decode_data(fin, fout, finfo);
    write_buffer(fout);
    if (fout->status || result)
        perror(msg_fwerr);
    return result;
}

int compress_file(struct file *fin, struct file *fout,  struct fileinfo *finfo)
{
    int res = 0;

    analyze_input_file(fin, finfo->ctable);
    calc_ufilesize(finfo);
    if (finfo->ufsize == 0) {
        fputs(msg_infempt, stderr);
        return 2;
    }

    calc_chars_count(finfo);
    create_tree_leafs(finfo);
    fill_leafs(finfo);
    build_tree(finfo);
    char_codes(*(finfo->stree));
    res += write_info(finfo, fout);
    res += write_data(fin, fout, finfo);
    write_buffer(fout);
    if (fout->status || res) {
        perror(msg_fwerr);
        return 2;
    } else {
        return 0;
    }
}

int open_file(struct file *fl, const char *mode)
{
    fl->fd = fopen(fl->fname, mode);
    if (!fl->fd) {
        perror(fl->fname);
        return 1;
    }
    else {
        return 0;
    }
}

void buffer_init(struct io_buffer *buffer, unsigned char *mem)
{
    buffer->buffer = mem;
    buffer->posl = 0;
    buffer->posf = 0;
}

int processing(struct cmd_options *opts)
{
    struct file fin;
    struct file fout;
    struct fileinfo finfo;
    struct io_buffer rbuf, wbuf;
    unsigned char rmem[inbuf_size];
    unsigned char wmem[outbuf_size];
    int result = 0;

    buffer_init(&rbuf, rmem);
    buffer_init(&wbuf, wmem);

    fin.status = 0;
    fin.fname = opts->input_fname;
    fin.ibuf = &rbuf;
    fout.status = 0;
    fout.fname = opts->output_fname;
    fout.outbuf = &wbuf;

    init_finfo(&finfo);

    result = open_file(&fin, "rb");
    if (result)
        return 1;
    result = open_file(&fout, "wb");
    if (result)
        return 1;

    if (opts->options == opt_compress) {
        result = compress_file(&fin, &fout, &finfo);
        if (result)
            fputs(msg_cerr, stderr);
    }
    if (opts->options == opt_extract) {
        result = extract_file(&fin, &fout, &finfo);
        if (result)
            fputs(msg_xerr, stderr);
    }
    fclose(fin.fd);
    fclose(fout.fd);
    return result;
}

void print_help()
{
    fputs(
        "sarch - simple archiver\n"
        "\n"
        "Usage: sarch [options] [input file] [output file]\n"
        "\n"
        "Options:\n"
        "  -c   compress file\n"
        "  -x   extract file\n"
        "  -h   show this help\n"
        "  -v   show version\n", stdout);
}

void print_version()
{
    fputs("Version\n", stdout);
}

void print_few_args()
{
    fputs(
        "Too few arguments\n"
        "Try -h for help\n", stderr);
}

void print_incorrect_opt(const char *str)
{
    fprintf(stderr,
        "Incorrect option \"%s\"\n"
        "Try -h for help\n", str);
}

void print_nooptions()
{
    fputs(
        "No options\n"
        "Try -h for help\n", stderr);
}

void print_opts_conflict()
{
    fputs(
        "Conflict options. One the following must be selected: \"-c -x\"\n"
        "Try -h for help\n", stderr);
}

void print_noinput_file()
{
    fputs(
        "No input or output file\n"
        "Nothing to do\n", stderr);
}

int conflict_options(int options)
{
    int result = 0;

    result += options & opt_compress ? 1 : 0;
    result += options & opt_extract ? 1 : 0;
    if (result > 1)
        return result;
    else
        return 0;
}

void init_opts(struct cmd_options *opts)
{
    memset(opts, 0, sizeof(struct cmd_options));
}

int parsing_opts(int argc, char **argv, struct cmd_options *opts)
{
    int nopt = 1;
    if (argc < 2) {
        print_few_args();
        return 1;
    }
    while (nopt < argc) {
        if (*argv[nopt] == '-') {
            if (strlen(argv[nopt]) > 2) {
                print_incorrect_opt(argv[nopt]);
                return 1;
            }
            switch (argv[nopt][1]) {
                case 'c':
                    opts->options |= opt_compress;
                    break;
                case 'x':
                    opts->options |= opt_extract;
                    break;
                case 'h':
                    print_help();
                    exit(0);
                case 'v':
                    print_version();
                    exit(0);
                default:
                    print_incorrect_opt(argv[nopt]);
                    return 1;
            }
        } else {
            if (opts->input_fname == NULL) {
                opts->input_fname = argv[nopt];
            } else {
                if (opts->output_fname == NULL) {
                    opts->output_fname = argv[nopt];
                } else {
                    print_incorrect_opt(argv[nopt]);
                    return 1;
                }
            }
        }
        nopt++;
    }

    if (opts->input_fname == NULL || opts->output_fname == NULL) {
        print_noinput_file();
        return 1;
    }

    if (conflict_options(opts->options)) {
        print_opts_conflict();
        return 1;
    }

    if (opts->options == 0) {
        print_nooptions();
        return 1;
    }

    return 0;
}

int main(int argc, char **argv)
{
    struct cmd_options opts;
    int res;

    init_opts(&opts);
    res = parsing_opts(argc, argv, &opts);
    if (res)
        return 1;

    return processing(&opts);
} 
