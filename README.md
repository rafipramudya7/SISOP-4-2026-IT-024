# kenz_rescue

Soal fuse untuk me mount folder dan membuat virtual file dengan menggabungkan KOORD dari semua txt

---
# Penjelasan Kode: kenz_rescue.c

Dokumen ini menjelaskan struktur dan logika internal dari `kenz_rescue.c` secara mendetail, fungsi per fungsi.

---

## Gambaran Umum

`kenz_rescue.c` adalah program FUSE (Filesystem in Userspace) yang:
- Me-mount sebuah direktori sumber ke mount point tertentu
- Meneruskan akses file secara transparan (pass-through read-only)
- Menyuntikkan satu file virtual bernama `tujuan.txt` yang isinya dikumpulkan secara dinamis dari file-file di direktori sumber

---

## Preprocessor & Include

```c
#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
```

- `FUSE_USE_VERSION 31` —  menggunakan API versi 3.1.


---

## Variabel Global

```c
static char source_dir[PATH_MAX];
```

Menyimpan **path absolut** dari direktori sumber yang diberikan via argumen CLI.

---

## Fungsi `fullpath()`

```c
static void fullpath(char out[PATH_MAX], const char *fuse_path)
{
    snprintf(out, PATH_MAX, "%s%s", source_dir, fuse_path);
}
```

**Tujuan:** Mengonversi path virtual FUSE  menjadi path nyata di filesystem host 



---

## Fungsi `build_tujuan()`

```c
static char *build_tujuan(size_t *out_len)
```

**Tujuan:** Membangun isi dari file virtual `tujuan.txt` secara dinamis setiap kali file tersebut dibaca.

**Langkah-langkah:**

### 1. Iterasi file `1.txt` – `7.txt`
```c
for (int i = 1; i <= 7; i++)
{
    char path[PATH_MAX * 2];
    snprintf(path, sizeof(path), "%s/%d.txt", source_dir, i);
    FILE *f = fopen(path, "r");
    if (!f) continue;
    ...
}
```
Loop membuka file bernomor 1 sampai 7. Jika file tidak ditemukan maka lewati

### 2. Mencari baris `KOORD:`
```c
while (fgets(line, sizeof(line), f))
{
    if (strncmp(line, "KOORD: ", 7) == 0)
    {
        char *val = line + 7;   
        ...
        break;                  
    }
}
```
Hanya baris yang diawali `KOORD: ` (7 karakter) yang diambil. Setelah ditemukan, langsung `break` — hanya satu nilai per file.

### 3. Strip newline & akumulasi
```c
while (vlen > 0 && (val[vlen-1] == '\n' || val[vlen-1] == '\r'))
    vlen--;

memcpy(fragments + frag_pos, val, vlen);
frag_pos += vlen;
```
Karakter newline dihapus dari akhir nilai, lalu nilai diappend ke buffer `fragments` tanpa pemisah semua nilai disatukan langsung.

### 4. Format hasil akhir
```c
snprintf(result, total, "%s%s\n", prefix, fragments);
```
Output akhir berformat:
```
Tujuan Mas Amba: <nilai1><nilai2>...<nilai7>
```
diakhiri newline `\n`. Caller bertanggung jawab untuk `free()` pointer yang dikembalikan.

---

## Fungsi `kenz_getattr()`

```c
static int kenz_getattr(const char *path, struct stat *st,
                        struct fuse_file_info *fi)
```

**Tujuan:** Menjawab pertanyaan kernel "seperti apa metadata file ini?" — seperti dengan syscall `stat()`.

---

## Fungsi `kenz_readdir()`

```c
static int kenz_readdir(const char *path, void *buf,
                        fuse_fill_dir_t filler, off_t offset,
                        struct fuse_file_info *fi,
                        enum fuse_readdir_flags flags)
```

**Tujuan:** Mengembalikan daftar isi direktori — ekuivalen dengan `ls`.

**Logika:**

```c
if (strcmp(path, "/") != 0)
    return -ENOENT;   
```

Filesystem ini hanya punya satu level direktori 

```c
filler(buf, ".", NULL, 0, 0);    // direktori ini sendiri
filler(buf, "..", NULL, 0, 0);   // direktori parent
```

Dua entri wajib yang selalu ada.

```c
DIR *dp = opendir(source_dir);
while ((de = readdir(dp)) != NULL) {
    if (de->d_name[0] == '.') continue;   // sembunyikan file hidden
    filler(buf, de->d_name, NULL, 0, 0);
}
```

Membaca isi direktori sumber dan meneruskannya ke mount point. File hidden (diawali `.`) disaring.

```c
filler(buf, "tujuan.txt", NULL, 0, 0);   // injeksi file virtual
```

File virtual `tujuan.txt` ditambahkan secara manual di akhir listing — tidak ada di disk, tapi terlihat di `ls`.

---

## Fungsi `kenz_open()`

```c
static int kenz_open(const char *path, struct fuse_file_info *fi)
```

**Tujuan:** Memvalidasi apakah file boleh dibuka dengan flag yang diminta.

**Logika untuk `tujuan.txt`:**
```c
if ((fi->flags & O_ACCMODE) != O_RDONLY)
    return -EACCES;   
return 0;
```

File virtual hanya boleh dibuka untuk dibaca. Percobaan write/append akan mendapat error `EACCES`.

**Logika untuk file diluar itu:**
```c
int fd = open(fpath, fi->flags);
if (fd == -1) return -errno;
close(fd);   
return 0;
```

File descriptor dibuka hanya untuk memvalidasi akses, lalu langsung ditutup. FD yang sesungguhnya akan dibuka ulang di `kenz_read()`. 

---

## Fungsi `kenz_read()`

```c
static int kenz_read(const char *path, char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
```

**Tujuan:** Membaca isi file — sama seprti `read()` / `pread()`.

**Logika untuk `tujuan.txt`:**
```c
char *content = build_tujuan(&len);

if ((size_t)offset < len) {
    size_t available = len - (size_t)offset;
    bytes_read = (int)(available < size ? available : size);
    memcpy(buf, content + offset, bytes_read);
}
free(content);
return bytes_read;
```

isi dibangun ulang setiap kali dibaca. Mendukung **partial read** via `offset` 

**Logika untuk file lain:**
```c
int fd = open(fpath, O_RDONLY);
int res = (int)pread(fd, buf, size, offset);
close(fd);
return res;
```

Menggunakan `pread()`  agar pembacaan  bisa dilakukan tanpa perlu menggeser posisi file cursor terlebih dahulu

---

## Registrasi Operasi FUSE

```c
static const struct fuse_operations kenz_ops = {
    .getattr = kenz_getattr,
    .readdir = kenz_readdir,
    .open    = kenz_open,
    .read    = kenz_read,
};
```

Struct ini adalah "daftar isi" yang memberitahu library FUSE fungsi mana yang menangani operasi apa. 

---

## Fungsi `main()`

```c
int main(int argc, char *argv[])
```

**Langkah-langkah:**

### 1. Validasi argumen
```c
```

### 2. Resolve path absolut
```c
if (!realpath(argv[1], source_dir)) { ... }
```
`realpath()` mengonversi path relatif menjadi absolut dan memastikan direktori tersebut benar-benar ada. Hasilnya disimpan ke `source_dir`.

### 3. Susun ulang `argv` untuk FUSE
```c
fuse_argv[0] = argv[0];        
for (int i = 1; i < fuse_argc; i++)
    fuse_argv[i] = argv[i + 1];  
```
FUSE tidak tahu soal argumen `source_dir`  itu khusus untuk program kita. Maka `argv` disusun ulang: `source_dir` dibuang, sisanya diteruskan ke `fuse_main()`.

### 4. Serahkan ke FUSE
```c
int ret = fuse_main(fuse_argc, fuse_argv, &kenz_ops, NULL);
```
`fuse_main()` adalah entry point library FUSE yang mengurus daemonisasi, mounting, dan event loop. Program tidak akan kembali dari sini sampai filesystem di-unmount.

---
