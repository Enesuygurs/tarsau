#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#define MAX_DOSYA  32
#define MAX_BOYUT  (200 * 1024 * 1024)  // 200 MB

// ASCII ve 1 bayt kontrolu
int textMi(char *dosya) {
    FILE *f = fopen(dosya, "rb");
    if (!f) return 0;

    int c;
    while ((c = fgetc(f)) != EOF) {
        // 0-127 disindaki karakterleri (genisletilmis ascii vb) reddet
        if (c > 127) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

// Dosya boyutu
long boyutAl(char *dosya) {
    struct stat s;
    if (stat(dosya, &s) == 0) return s.st_size;
    return -1;
}

// Dosya izinlerini (octal) al
int izinAl(char *dosya) {
    struct stat s;
    if (stat(dosya, &s) != 0) return 0644;
    return s.st_mode & 0777;
}

// Tam yoldan sadece dosya adini cikar (orn: test/a.txt -> a.txt)
char *isimAl(char *yol) {
    char *p = strrchr(yol, '/');
    if (!p) p = strrchr(yol, '\\');
    return p ? p + 1 : yol;
}

// Hedef dizini (alt dizinler dahil) olustur
void dizinOlustur(const char *yol) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", yol);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
#ifdef _WIN32
            mkdir(tmp);
#else
            mkdir(tmp, 0755);
#endif
            *p = '/';
        }
    }
#ifdef _WIN32
    mkdir(tmp);
#else
    mkdir(tmp, 0755);
#endif
}

// -b ile arsiv olusturma
int arsivOlustur(int sayac, char *dosyalar[], char *cikti) {
    int i;
    long toplamBoyut = 0;
    char buf[1024];
    
    if (sayac == 0) {
        printf("Hata: dosya belirtilmedi!\n");
        return 1;
    }
    if (sayac > MAX_DOSYA) {
        printf("Hata: en fazla %d dosya arsivlenebilir\n", MAX_DOSYA);
        return 1;
    }

    // dosyalari kontrol et
    for (i = 0; i < sayac; i++) {
        FILE *tf = fopen(dosyalar[i], "rb");
        if (!tf) {
            printf("%s bulunamadi!\n", dosyalar[i]);
            return 1;
        }
        fclose(tf);

        if (!textMi(dosyalar[i])) {
            fprintf(stderr, "%s giris dosyasinin formati uyumsuzdur!\n", dosyalar[i]);
            return 1;
        }
        long b = boyutAl(dosyalar[i]);
        if (b < 0) {
            printf("%s okunamiyor!\n", dosyalar[i]);
            return 1;
        }
        toplamBoyut += b;
    }
    
    if (toplamBoyut > MAX_BOYUT) {
        printf("Hata: toplam boyut 200MB sinirini asiyor!\n");
        return 1;
    }

    // once org bolumunun boyutunu hesapla
    // format: |dosyaadi,izin,boyut|dosyaadi2,izin2,boyut2|
    int orgBoyut = 0;
    for (i = 0; i < sayac; i++) {
        char *ad = isimAl(dosyalar[i]);
        int izin = izinAl(dosyalar[i]);
        long boy = boyutAl(dosyalar[i]);
        sprintf(buf, "|%s,%04o,%ld", ad, izin, boy);
        orgBoyut += strlen(buf);
    }
    orgBoyut++; // son | icin

    // org bolumunu olustur
    char *orgBolum = malloc(orgBoyut + 1);
    if (!orgBolum) {
        printf("bellek hatasi!\n");
        return 1;
    }
    orgBolum[0] = '\0';
    
    for (i = 0; i < sayac; i++) {
        char *ad = isimAl(dosyalar[i]);
        int izin = izinAl(dosyalar[i]);
        long boy = boyutAl(dosyalar[i]);
        sprintf(buf, "|%s,%04o,%ld", ad, izin, boy);
        strcat(orgBolum, buf);
    }
    strcat(orgBolum, "|");

    // arsiv dosyasini yaz
    FILE *out = fopen(cikti, "wb");
    if (!out) {
        printf("%s olusturulamadi!\n", cikti);
        free(orgBolum);
        return 1;
    }

    // 10 byte header (org bolumu boyutu)
    fprintf(out, "%010d", orgBoyut);
    
    // org bolumunu yaz
    fwrite(orgBolum, 1, orgBoyut, out);
    free(orgBolum);

    // dosya iceriklerini ardindan yaz
    for (i = 0; i < sayac; i++) {
        FILE *in = fopen(dosyalar[i], "rb");
        if (in) {
            int ch;
            while ((ch = fgetc(in)) != EOF) {
                fputc(ch, out);
            }
            fclose(in);
        }
    }

    fclose(out);
    printf("%s arsivi olusturuldu.\n", cikti);
    return 0;
}

// -a ile arsiv acma
int arsivAc(char *arsivAdi, char *dizin) {
    int i;
    
    // uzanti kontrolu
    char *uzanti = strrchr(arsivAdi, '.');
    if (!uzanti || strcmp(uzanti, ".sau") != 0) {
        fprintf(stderr, "Arsiv dosyasi uygunsuz veya bozuk!\n");
        return 1;
    }

    FILE *fp = fopen(arsivAdi, "rb");
    if (!fp) {
        fprintf(stderr, "Arsiv dosyasi uygunsuz veya bozuk!\n");
        return 1;
    }

    // ilk 10 byte header
    char header[11];
    if (fread(header, 1, 10, fp) != 10) {
        fprintf(stderr, "Arsiv dosyasi uygunsuz veya bozuk!\n");
        fclose(fp);
        return 1;
    }
    header[10] = '\0';
    int orgBoyut = atoi(header);
    
    if (orgBoyut <= 0) {
        fprintf(stderr, "Arsiv dosyasi uygunsuz veya bozuk!\n");
        fclose(fp);
        return 1;
    }

    // org bolumunu oku
    char *orgBolum = malloc(orgBoyut + 1);
    if (!orgBolum) {
        printf("bellek hatasi\n");
        fclose(fp);
        return 1;
    }
    if (fread(orgBolum, 1, orgBoyut, fp) != orgBoyut) {
        fprintf(stderr, "Arsiv dosyasi uygunsuz veya bozuk!\n");
        free(orgBolum);
        fclose(fp);
        return 1;
    }
    orgBolum[orgBoyut] = '\0';

    // dizin varsa olustur
    if (dizin != NULL && strlen(dizin) > 0)
        dizinOlustur(dizin);

    // org bolumunu parcala
    // dosya bilgilerini tutacak diziler
    char adlar[MAX_DOSYA][256];
    int izinler[MAX_DOSYA];
    long boyutlar[MAX_DOSYA];
    int dosyaSayisi = 0;

    char *ptr = orgBolum;
    if (*ptr == '|') ptr++; // bastaki | atla

    while (*ptr && dosyaSayisi < MAX_DOSYA) {
        char *bitis = strchr(ptr, '|');
        if (!bitis) break;
        
        int len = bitis - ptr;
        if (len <= 0) {
            ptr = bitis + 1;
            continue;
        }

        // kaydi gecici buffer'a kopyala
        char kayit[512];
        strncpy(kayit, ptr, len);
        kayit[len] = '\0';

        // virgullerden ayir: ad,izin,boyut
        char *v1 = strchr(kayit, ',');
        if (!v1) break;
        *v1 = '\0';
        
        char *v2 = strchr(v1 + 1, ',');
        if (!v2) break;
        *v2 = '\0';

        strcpy(adlar[dosyaSayisi], kayit);
        izinler[dosyaSayisi] = strtol(v1 + 1, NULL, 8);
        boyutlar[dosyaSayisi] = atol(v2 + 1);
        
        dosyaSayisi++;
        ptr = bitis + 1;
    }

    if (dosyaSayisi == 0) {
        fprintf(stderr, "Arsiv dosyasi uygunsuz veya bozuk!\n");
        free(orgBolum);
        fclose(fp);
        return 1;
    }

    // dosyalari cikar
    for (i = 0; i < dosyaSayisi; i++) {
        char yol[1024];
        
        if (dizin && strlen(dizin) > 0)
            sprintf(yol, "%s/%s", dizin, adlar[i]);
        else
            strcpy(yol, adlar[i]);

        FILE *out = fopen(yol, "wb");
        if (!out) {
            printf("%s olusturulamadi!\n", yol);
            free(orgBolum);
            fclose(fp);
            return 1;
        }

        long kalan = boyutlar[i];
        while (kalan > 0) {
            int ch = fgetc(fp);
            if (ch == EOF) {
                fprintf(stderr, "Arsiv dosyasi uygunsuz veya bozuk!\n");
                fclose(out);
                free(orgBolum);
                fclose(fp);
                return 1;
            }
            fputc(ch, out);
            kalan--;
        }
        fclose(out);

        // izinleri uygula (sadece unix tabanli sistemlerde tam calisir)
#ifndef _WIN32
        chmod(yol, izinler[i]);
#endif
        printf("%s cikarildi\n", adlar[i]);
    }

    printf("Arsiv acildi.\n");
    free(orgBolum);
    fclose(fp);
    return 0;
}

int main(int argc, char *argv[]) {
    int i;
    
    if (argc < 3) {
        printf("Kullanim:\n");
        printf("  tarsau -b dosya1 dosya2 ... [-o arsiv.sau]\n");
        printf("  tarsau -a arsiv.sau [dizin]\n");
        return 1;
    }

    if (strcmp(argv[1], "-b") == 0) {
        char *ciktiAdi = "a.sau";
        int dsayisi = 0;
        char *dlist[MAX_DOSYA];

        for (i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) {
                if (i + 1 < argc) {
                    ciktiAdi = argv[++i];
                } else {
                    printf("Hata: -o parametresinden sonra dosya adi belirtilmeli!\n");
                    return 1;
                }
            } else {
                if (dsayisi >= MAX_DOSYA) {
                    printf("Hata: en fazla %d dosya arsivlenebilir\n", MAX_DOSYA);
                    return 1;
                }
                dlist[dsayisi++] = argv[i];
            }
        }
        return arsivOlustur(dsayisi, dlist, ciktiAdi);
    } 
    else if (strcmp(argv[1], "-a") == 0) {
        if (argc > 4) {
            printf("Kullanim: tarsau -a arsiv.sau [dizin]\n");
            return 1;
        }
        char *arsiv = argv[2];
        char *dizin = (argc == 4) ? argv[3] : NULL;
        return arsivAc(arsiv, dizin);
    } 
    else {
        printf("Bilinmeyen parametre: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
