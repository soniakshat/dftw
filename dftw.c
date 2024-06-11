#define _XOPEN_SOURCE 500
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <fcntl.h>

static int file_count = 0;
static int dir_count = 0;
static long long total_file_size = 0;
static char *src_dir;
char dst_dir[PATH_MAX];
static char *avoid_ext;

// Function to print usage information
void usage(const char *progname)
{
    fprintf(stderr, "\nUsage: %s -nf <source_dir>\n", progname);
    fprintf(stderr, "       %s -nd <source_dir>\n", progname);
    fprintf(stderr, "       %s -sf <source_dir>\n", progname);
    fprintf(stderr, "       %s -cpx <source_dir> <destination_dir> [file_extension_to_avoid]\n", progname);
    fprintf(stderr, "       %s -mv <source_dir> <destination_dir>\n", progname);
    exit(EXIT_FAILURE);
}

// Function to check if a directory exists
int directory_exists(const char *path)
{
    struct stat sb;
    return (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode));
}

// Callback function for nftw for counting files
int count_files(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    if (typeflag == FTW_F)
    {
        file_count++;
    }
    return 0;
}

// Function to count the files in the directory
int count_files_in_directory(const char *path)
{
    file_count = 0;

    if (nftw(path, count_files, 20, 0) == -1)
    {
        perror("nftw");
        return -1;
    }

    return file_count;
}

// Callback function for nftw for counting directories
int count_dirs(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    if (typeflag == FTW_D)
    {
        dir_count++;
    }
    return 0;
}

// Function to count the directories in the directory
int count_dirs_in_directory(char *dirpath)
{
    src_dir = dirpath;
    dir_count = -1; // -1 to avoid counting the source directory itself

    if (nftw(dirpath, count_dirs, 20, 0) == -1)
    {
        perror("nftw");
        return -1;
    }

    return dir_count;
}

// Callback function for calculating file sizes
int calculate_file_size(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    if (typeflag == FTW_F)
    {
        total_file_size += sb->st_size;
    }
    return 0;
}

// Function to calculate the total size of files in the directory
long long calculate_total_file_size(const char *dirpath)
{
    total_file_size = 0;

    if (nftw(dirpath, calculate_file_size, 20, 0) == -1)
    {
        perror("nftw");
        return -1;
    }

    return total_file_size;
}

// Function to create directories recursively
void make_path(const char *path)
{
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for (p = tmp + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

// Function to copy a file
int copy_file(const char *src, const char *dst)
{
    int input, output;
    if ((input = open(src, O_RDONLY)) == -1)
    {
        perror("open");
        return -1;
    }
    if ((output = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1)
    {
        perror("open");
        close(input);
        return -1;
    }

    char buffer[4096];
    ssize_t bytes;
    while ((bytes = read(input, buffer, sizeof(buffer))) > 0)
    {
        if (write(output, buffer, bytes) != bytes)
        {
            perror("write");
            close(input);
            close(output);
            return -1;
        }
    }

    close(input);
    close(output);

    return 0;
}

// Callback function for nftw for copying files and directories
int copy_callback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    char rel_path[PATH_MAX];
    char dest_path[PATH_MAX];

    snprintf(rel_path, sizeof(rel_path), "%s", fpath + strlen(src_dir));
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dst_dir, rel_path);

    if (typeflag == FTW_D)
    {
        make_path(dest_path);
    }
    else if (typeflag == FTW_F)
    {
        if (avoid_ext != NULL)
        {
            if ((strcmp(avoid_ext, ".pdf") == 0 || strcmp(avoid_ext, ".txt") == 0 || strcmp(avoid_ext, ".c") == 0))
            {
                if (avoid_ext && strlen(avoid_ext) > 0 && strstr(fpath, avoid_ext) != NULL)
                {
                    return 0;
                }
            }
            else
            {
                fprintf(stderr, "\nOnly .txt, .c and .pdf extensions can be excluded.\n");
                return -1;
            }
        }
        make_path(dirname(strdup(dest_path)));
        if (copy_file(fpath, dest_path) == -1)
        {
            return -1;
        }
    }
    return 0;
}

// Function to copy a directory and its contents
int copy_directory(const char *source_directory, const char *destination_directory, const char *extension_to_avoid)
{
    src_dir = strdup(source_directory);

    snprintf(dst_dir, sizeof(dst_dir), "%s/%s", destination_directory, basename(strdup(src_dir)));
    avoid_ext = (char *)extension_to_avoid;

    if (nftw(src_dir, copy_callback, 64, FTW_PHYS) == -1)
    {
        perror("nftw");
        free(src_dir);
        return -1;
    }

    free(src_dir);
    return 0;
}

// Callback function for nftw for removing files and directories
int remove_callback(const char *path, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
    int ret = remove(path);
    if (ret)
    {
        perror(path);
    }
    return ret;
}

// Function to delete a directory and all its contents
int delete_directory(const char *path)
{
    if (nftw(path, remove_callback, 64, FTW_DEPTH | FTW_PHYS) == -1)
    {
        perror("nftw");
        return -1;
    }
    return 0;
}

// Function to move a directory and its contents
int move_directory(const char *src, const char *dest)
{
    int res = copy_directory(src, dest, NULL);
    if (res == 0)
    {
        return delete_directory(src);
    }
    else
    {
        return -1;
    }
}

// Entrypoint to program
int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        usage(argv[0]);
    }

    char *option = argv[1];
    char *root_dir = argv[2];

    if (directory_exists(root_dir) == -1)
    {
        fprintf(stderr, "Source directory does not exist: %s\n", root_dir);
        exit(EXIT_FAILURE);
    }

    if (strcmp(option, "-nf") == 0)
    {
        int result = count_files_in_directory(root_dir);
        if (result == -1)
        {
            fprintf(stderr, "\nError counting files in directory: %s\n", root_dir);
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("\nTotal files found: %d\n", result);
        }
    }
    else if (strcmp(option, "-nd") == 0)
    {
        int result = count_dirs_in_directory(root_dir);
        if (result == -1)
        {
            fprintf(stderr, "\nError counting directories in directory: %s\n", root_dir);
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("\nTotal directories found: %d\n", result);
        }
    }
    else if (strcmp(option, "-sf") == 0)
    {
        long long size_result = calculate_total_file_size(root_dir);
        if (size_result == -1)
        {
            fprintf(stderr, "\nError calculating total file size in directory: %s\n", root_dir);
            exit(EXIT_FAILURE);
        }
        else if (size_result == 0)
        {
            printf("\nNo files in the provided path and its sub-paths.\n");
            exit(EXIT_SUCCESS);
        }
        else
        {
            printf("\nTotal size of all files: %lld bytes\n", size_result);
        }
    }
    else if (strcmp(option, "-cpx") == 0)
    {
        if (argc < 4)
        {
            usage(argv[0]);
        }
        char *des_dir = argv[3];
        if (directory_exists(des_dir) == -1)
        {
            fprintf(stderr, "\nDestination directory does not exist: %s\n", des_dir);
            exit(EXIT_FAILURE);
        }

        char *avoid = (argc > 4) ? argv[4] : NULL;
        if (copy_directory(root_dir, des_dir, avoid) == -1)
        {
            fprintf(stderr, "\nError copying directory from %s to %s\n", root_dir, des_dir);
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("\nCopy from %s to %s finished properly\n", root_dir, des_dir);
        }
    }
    else if (strcmp(option, "-mv") == 0)
    {
        if (argc < 4)
        {
            usage(argv[0]);
        }
        char *des_dir = argv[3];
        if (directory_exists(des_dir) == -1)
        {
            fprintf(stderr, "\nDestination directory does not exist: %s\n", des_dir);
            exit(EXIT_FAILURE);
        }

        if (move_directory(root_dir, des_dir) == -1)
        {
            fprintf(stderr, "\nError moving directory from %s to %s\n", root_dir, des_dir);
            exit(EXIT_FAILURE);
        }
        else
        {
            printf("\nMove from %s to %s finished properly\n", root_dir, des_dir);
        }
    }
    else
    {
        usage(argv[0]);
    }

    return 0;
}