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
`

# Penjelasan Kode: LibraryIT Server

 menjelaskan struktur dan logika internal dari setiap file dalam project LibraryIT Server secara mendetail.

---

## Gambaran Umum

Project ini terdiri dari 4 file yang bekerja bersama:

```
Dockerfile          → membangun image Docker berisi Samba
smb.conf            → konfigurasi share dan hak akses Samba
entrypoint.sh       → setup user/folder/permission saat container start
docker-compose.yml  → orkestrasi container server + logger
```

---

## `Dockerfile`

```dockerfile
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
```

Basis image nya Ubuntu 22.04. `DEBIAN_FRONTEND=noninteractive` mencegah `apt` menampilkan prompt interaktif saat proses build  wajib diset agar build tidak hang menunggu input.

```dockerfile
RUN apt update && apt install -y \
    samba \
    samba-common-bin \
    acl \
    && rm -rf /var/lib/apt/lists/*
```

Menginstall 3 paket:

- `samba` — daemon server SMB/CIFS utama (`smbd`)
- `samba-common-bin` — tools pendukung termasuk `smbpasswd` untuk mengelola password Samba
- `acl` — tools untuk Access Control List di filesystem

Cache apt (`/var/lib/apt/lists/*`) dihapus di akhir **dalam satu `RUN` yang sama** agar tidak membuat layer Docker tambahan yang membengkakkan ukuran image.

```dockerfile
COPY smb.conf /etc/samba/smb.conf
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
EXPOSE 139 445
CMD ["/entrypoint.sh"]
```

- `smb.conf` disalin ke lokasi default yang dibaca Samba secara otomatis
- `entrypoint.sh` diberi izin eksekusi lewat `chmod +x`
- Port `445` (SMB modern) dan `139` (NetBIOS legacy) dideklarasikan
- `CMD` menentukan perintah yang dijalankan saat container pertama start

---

## `entrypoint.sh`

Script ini dieksekusi setiap kali container start. Tugasnya menyiapkan semua infrastruktur sebelum Samba berjalan.

### `set -e`

```bash
set -e
```

Menyebabkan script **langsung berhenti** jika ada perintah yang gagal (exit code non-zero). Ini mencegah Samba berjalan dalam kondisi setengah jadi jika salah satu langkah setup gagal.

### Membuat Direktori

```bash
mkdir -p /libraryit/ebooks
mkdir -p /libraryit/papers
mkdir -p /libraryit/sourcecode
mkdir -p /libraryit/docs
mkdir -p /logs
touch /logs/libraryit.log
```

Membuat 4 folder konten dan file log. Flag `-p` memastikan tidak error jika folder sudah ada. `touch` membuat file log kosong agar `tail -f` di container logger tidak langsung error.

### Membuat Group

```bash
groupadd -f readonly
groupadd -f staff
```

Dua group sebagai hak akses. Flag `-f` (force) mencegah error jika group sudah ada. Group ini direferensikan di `smb.conf` dengan prefix `@` 

### Membuat User Linux

```bash
id member >/dev/null 2>&1 || useradd -m member
id contributor >/dev/null 2>&1 || useradd -m contributor
id librarian >/dev/null 2>&1 || useradd -m librarian
```



### Set Password Linux & Samba

```bash
echo "member:member123" | chpasswd
(echo member123; echo member123) | smbpasswd -s -a member
```

Setiap user mendapat **dua password yang diset terpisah** karena Samba menyimpan credential di database sendiri (`/var/lib/samba/private/passdb.tdb`), terpisah dari `/etc/shadow` milik Linux.

- `chpasswd` — set password Linux, menerima format `user:password` via stdin
- `smbpasswd -s -a` — set password Samba; `-s` = silent/non-interaktif (baca dari stdin), `-a` = tambah user baru ke database Samba
- Dua `echo` di dalam subshell `(...)` mensimulasikan input "masukkan password" dan "konfirmasi password" yang diminta `smbpasswd`

### Assign User ke Group

```bash
usermod -aG readonly member
usermod -aG staff contributor
usermod -aG staff librarian
```

Menentukan hak akses tiap user. Flag `-aG` berarti "append to Group" — menambahkan ke group tanpa menghapus keanggotaan group lain yang sudah ada.


### Set Permission Folder

```bash
chmod -R 770 /libraryit/ebooks    
chmod -R 770 /libraryit/papers
chmod -R 750 /libraryit/sourcecode   
chmod -R 755 /libraryit/docs         
```

Permission filesystem ini menjadi lapisan keamanan kedua di bawah Samba. Meski seseorang berhasil masuk ke container, mereka tetap dibatasi oleh permission Unix ini.

### Menjalankan Samba

```bash
exec smbd --foreground --no-process-group
```

`exec` menggantikan proses shell (`bash`) dengan `smbd` sehingga `smbd` menjadi **PID 1** di container. Ini penting karena Docker mengirim sinyal (SIGTERM saat `docker stop`) ke PID 1. Jika `exec` tidak dipakai, sinyal dikirim ke `bash`, bukan ke `smbd`, dan Samba tidak bisa shutdown dengan bersih.

- `--foreground` — mencegah `smbd` melakukan daemonisasi (fork ke background), karena Docker memantau proses foreground
- `--no-process-group` — mencegah `smbd` membuat process group baru, agar sinyal dari Docker langsung diterima

---

## `smb.conf`

File konfigurasi utama Samba, dibagi menjadi blok `[global]` dan blok-blok share.

### Blok `[global]`

```ini
security = user
map to guest = never
```

`security = user` mengharuskan autentikasi username+password sebelum akses diberikan. `map to guest = never` memblokir total akses anonim — koneksi tanpa kredensial valid langsung ditolak.

```ini
log file = /logs/libraryit.log
max log size = 1000
logging = file
log level = 1
```

Log ditulis ke file yang sama dengan yang di-mount ke container logger. `max log size = 1000` membatasi ukuran log maksimum 1000 KB sebelum di-rotate. `log level = 1` adalah level minimal 

```ini
vfs objects = full_audit

full_audit:prefix = [%T] [%I] [%u]
full_audit:success = connect disconnect mkdir rmdir open write rename unlink
full_audit:failure = connect open write unlink rename
full_audit:facility = LOCAL7
full_audit:priority = NOTICE
```

`vfs objects = full_audit` mengaktifkan modul audit bawaan Samba. Modul ini mencegat setiap operasi file dan mencatatnya ke log.

- `prefix` menentukan format awal setiap baris log: `%T` = timestamp, `%I` = IP client, `%u` = username
- `success` — daftar operasi yang dicatat jika **berhasil**
- `failure` — daftar operasi yang dicatat jika **gagal** (subset dari success, fokus pada operasi sensitif)


### Blok Share

Setiap blok `[nama]` mendefinisikan satu folder yang bisa diakses via jaringan.

```ini
[ebooks]
   path = /libraryit/ebooks
   browseable = yes
   read only = no
   valid users = @staff @readonly
   write list = @staff
```

- `path` — lokasi folder di filesystem container
- `browseable = yes` — folder ini muncul saat client browse jaringan (`\\server\`)
- `read only = no` — izinkan akses tulis  
- `valid users` — hanya user dalam group `@staff` atau `@readonly` yang boleh masuk; `@` berarti group
- `write list` — dari yang sudah masuk, hanya `@staff` yang boleh menulis

```ini
[sourcecode]
   browseable = no
   valid users = @staff
```

`browseable = no` menyembunyikan share ini dari listing jaringan. User harus tahu nama share-nya untuk bisa mengakses. Hanya `@staff` yang boleh masuk `@readonly`.

```ini
[docs]
   read only = yes
   valid users = @staff @readonly
   write list = librarian
```

`read only = yes` membuat share ini read-only secara default untuk semua user. `write list = librarian` mengecualikan user `librarian` secara spesifik — user ini bisa menulis meski `read only = yes`. Perhatikan: `librarian` (tanpa `@`) berarti user individual, bukan group.

---

## `docker-compose.yml`

### Service `libraryit-server`

```yaml
build: .
ports:
  - "1445:445"
  - "1139:139"
```

`build: .` berarti Docker Compose akan membangun image dari `Dockerfile` di direktori yang sama. Port dipetakan ke `1445`/`1139` di host (bukan standar `445`/`139`) untuk menghindari konflik jika host sudah menjalankan Samba atau Windows file sharing.

```yaml
volumes:
  - ./data:/libraryit
  - ./logs:/logs
```

Dua bind mount yang membuat data **persisten di host**. Ketika container dihapus lalu dibuat ulang, semua file di `./data` dan log di `./logs` tetap ada. Pola `./host_path:/container_path` adalah bind mount.

```yaml
restart: unless-stopped
```

Container otomatis restart jika crash atau saat Docker daemon restart.

### Service `libraryit-logger`

```yaml
image: alpine:latest
command: sh -c "tail -f /logs/libraryit.log"
volumes:
  - ./logs:/logs
```

Container kedua yang hanya menjalankan `tail -f` untuk membaca log secara real-time.

```yaml
depends_on:
  - libraryit-server
```

Memastikan `libraryit-server` sudah start terlebih dahulu sebelum `libraryit-logger` dijalankan, sehingga file log sudah ada saat `tail -f` dimulai.

---

## Ringkasan Alur

```
docker compose up
       │
       ├─► libraryit-server
       │       │
       │       ├─ Dockerfile     → install Samba di atas Ubuntu 22.04
       │       ├─ smb.conf       → definisi share & aturan akses
       │       └─ entrypoint.sh
       │               ├─ buat /libraryit/{ebooks,papers,sourcecode,docs}
       │               ├─ buat group: readonly, staff
       │               ├─ buat user: member, contributor, librarian
       │               ├─ set password Linux (chpasswd) & Samba (smbpasswd)
       │               ├─ assign group: member→readonly, contributor/librarian→staff
       │               ├─ set chmod tiap folder
       │               └─ exec smbd (PID 1)
       │
       └─► libraryit-logger
               └─ tail -f ./logs/libraryit.log
                       ↑
               setiap akses SMB dari client dicatat di sini oleh full_audit
```