/* ac97.c — драйвер Intel AC'97 через Bus Master DMA.
 *
 * Регистры NABM (Bus Master), которые мы используем, все относятся
 * к блоку "PCM OUT" (воспроизведение) — это offset 0x10-0x1B от
 * базового адреса BAR1:
 *
 *   0x10 (BDBAR, 32 бита)  — физический адрес начала Buffer
 *                            Descriptor List (BDL)
 *   0x14 (CIV, 8 бит)      — Current Index Value: какая запись
 *                            BDL сейчас проигрывается (read-only)
 *   0x15 (LVI, 8 бит)      — Last Valid Index: последняя запись BDL,
 *                            которую можно проигрывать (мы пишем)
 *   0x16 (SR, 16 бит)      — Status Register (флаги состояния/прерываний)
 *   0x1B (CR, 8 бит)       — Control Register: бит RUN запускает DMA,
 *                            бит RESET сбрасывает канал
 *
 * Регистры NAM (Mixer), которые нужны нам для включения звука:
 *   0x00 — Reset (запись любого значения сбрасывает микшер)
 *   0x02 — Master Volume (общая громкость)
 *   0x18 — PCM Out Volume (громкость канала воспроизведения)
 *
 * Один элемент Buffer Descriptor List (8 байт, должен быть выровнен
 * на границу 8 байт по спецификации):
 *   +0: uint32_t pointer  — физический адрес буфера сэмплов
 *   +4: uint16_t length   — длина буфера В САМПЛАХ (не в байтах!)
 *   +6: uint16_t flags    — бит 15 (BUP) = "прервать проигрывание
 *                            здесь", бит 14 (IOC) = "прервать CPU
 *                            (interrupt) когда этот буфер доигран" —
 *                            прерывание мы не используем (опрашиваем
 *                            регистр сами), поэтому просто 0 */

#include "ac97.h"
#include "pci.h"
#include "io.h"
#include "vga.h"
#include "kmalloc.h"
#include "timer.h"

/* --- регистры микшера (NAM, BAR0) --- */
#define AC97_NAM_RESET       0x00
#define AC97_NAM_MASTER_VOL  0x02
#define AC97_NAM_PCM_VOL     0x18

/* --- регистры Bus Master (NABM, BAR1), канал PCM OUT --- */
#define AC97_NABM_PO_BDBAR   0x10 /* Buffer Descriptor Base Address */
#define AC97_NABM_PO_CIV     0x14 /* Current Index Value */
#define AC97_NABM_PO_LVI     0x15 /* Last Valid Index */
#define AC97_NABM_PO_SR      0x16 /* Status Register */
#define AC97_NABM_PO_CR      0x1B /* Control Register */

#define AC97_CR_RUN          0x01
#define AC97_CR_RESET        0x02

#define AC97_SR_DCH          0x01 /* DMA Controller Halted — DMA сейчас не бежит */

#define BDL_ENTRIES 32 /* максимум, сколько допускает спецификация AC97 */

struct __attribute__((packed)) ac97_bdl_entry {
    uint32_t pointer;
    uint16_t length;
    uint16_t flags;
};

static uint16_t nam_base;   /* базовый I/O-порт микшера (из BAR0) */
static uint16_t nabm_base;  /* базовый I/O-порт bus master (из BAR1) */
static int present = 0;

/* Buffer Descriptor List — выделяется один раз, переиспользуется на
 * каждое воспроизведение. Должен быть выровнен на 8 байт. */
static struct ac97_bdl_entry* bdl;

int ac97_is_present(void) {
    return present;
}

/* BAR-регистры PCI могут указывать либо на порты ввода-вывода (I/O
 * space, бит 0 = 1 — это как раз наш случай для AC97), либо на
 * область памяти (memory space, бит 0 = 0). Младший бит и нужно
 * замаскировать, чтобы получить реальный базовый адрес/порт. */
static uint16_t bar_to_io_port(uint32_t bar) {
    return (uint16_t)(bar & 0xFFFC);
}

int ac97_init(void) {
    struct pci_device dev;

    /* AC97 в QEMU по умолчанию — Intel 82801AA (Vendor 0x8086,
     * Device 0x2415). Но чтобы не завязываться жёстко на одну
     * модель, подстрахуемся и поищем ещё и по классу устройства:
     * класс 0x04 (Multimedia), подкласс 0x01 (Audio device) — так
     * найдём AC97 и на других эмулируемых чипсетах тоже. */
    int found = pci_find_device(0x8086, 0x2415, &dev);
    if (!found)
        found = pci_find_by_class(0x04, 0x01, &dev);

    if (!found) {
        terminal_writestring("ac97: PCI audio device not found\n");
        present = 0;
        return 0;
    }

    nam_base  = bar_to_io_port(dev.bar[0]);
    nabm_base = bar_to_io_port(dev.bar[1]);

    pci_enable_bus_mastering(&dev);

    /* Сбрасываем микшер (запись любого значения в регистр Reset —
     * по спецификации это возвращает все регистры микшера в состояние
     * по умолчанию) и даём чипу немного времени прийти в себя */
    outw(nam_base + AC97_NAM_RESET, 0x0000);
    timer_sleep_ms(10);

    /* Громкость: 0x0000 = максимальная громкость на обоих каналах
     * (у AC97 шкала громкости "инвертирована" — чем больше число,
     * тем ТИШЕ, поэтому 0 = самая громкая настройка) */
    outw(nam_base + AC97_NAM_MASTER_VOL, 0x0000);
    outw(nam_base + AC97_NAM_PCM_VOL, 0x0000);

    /* Сбрасываем канал PCM OUT перед использованием */
    outb(nabm_base + AC97_NABM_PO_CR, AC97_CR_RESET);
    timer_sleep_ms(10);

    /* Выделяем BDL один раз на весь дальнейший срок работы драйвера.
     * kmalloc даёт выравнивание только на 8 байт (ALIGN=8 в kmalloc.c),
     * а нам ровно 8 и нужно — но на всякий случай (если реализация
     * kmalloc изменится) подстрахуемся вручную. */
    uint8_t* raw = (uint8_t*) kmalloc(sizeof(struct ac97_bdl_entry) * BDL_ENTRIES + 8);
    if (!raw) {
        terminal_writestring("ac97: out of memory for BDL\n");
        present = 0;
        return 0;
    }
    uintptr_t addr = (uintptr_t) raw;
    addr = (addr + 7) & ~((uintptr_t)7); /* округляем вверх до кратного 8 */
    bdl = (struct ac97_bdl_entry*) addr;

    present = 1;
    terminal_writestring("ac97: audio device ready (bus master DMA)\n");
    return 1;
}

/* Ждём, пока канал PCM OUT остановится сам (доиграл всё, что было в
 * BDL) — опрашиваем бит DCH (DMA Controller Halted) в регистре
 * статуса. Простая, но рабочая замена обработчику прерывания. */
static void ac97_wait_dma_halted(void) {
    /* защищаемся от зависания навечно, если что-то пошло не так */
    for (uint32_t i = 0; i < 20000000u; i++) {
        uint16_t sr = inw(nabm_base + AC97_NABM_PO_SR);
        if (sr & AC97_SR_DCH)
            return;
    }
}

void ac97_play(const int16_t* samples, uint32_t sample_count) {
    if (!present || !samples || sample_count == 0)
        return;

    /* AC97 обращается к буферу сэмплов напрямую по физическому адресу
     * через DMA. Поскольку в этом ядре нет paging (виртуальные адреса
     * совпадают с физическими), указатель на массив можно передать
     * как есть — дополнительная трансляция адреса не нужна. */

    /* Один элемент BDL может описать не больше 0xFFFE сэмплов
     * (16-битное поле length), поэтому длинный звук режем на куски */
    uint32_t offset = 0;
    while (offset < sample_count) {
        uint32_t remaining = sample_count - offset;
        uint32_t chunk = remaining > 0xFFFE ? 0xFFFE : remaining;

        /* Заполняем один-единственный элемент BDL на этот кусок.
         * Флаг 0 означает "не прерывать CPU, просто остановиться
         * после этого буфера" (для простого блокирующего проигрывания
         * этого достаточно — дальше просто ждём DCH опросом). */
        bdl[0].pointer = (uint32_t)(uintptr_t)(samples + offset);
        bdl[0].length  = (uint16_t) chunk;
        bdl[0].flags   = 0x0000;

        outl(nabm_base + AC97_NABM_PO_BDBAR, (uint32_t)(uintptr_t) bdl);
        outb(nabm_base + AC97_NABM_PO_LVI, 0); /* используем только запись 0 из списка */
        outb(nabm_base + AC97_NABM_PO_CR, AC97_CR_RUN);

        ac97_wait_dma_halted();

        outb(nabm_base + AC97_NABM_PO_CR, 0x00); /* остановить канал перед следующим куском */

        offset += chunk;
    }
}

/* --- Генератор тестового тона: таблица значений синуса --- */

/* 256 отсчётов на один период — компромисс между точностью (больше
 * точек — точнее) и объёмом памяти/времени на построение (нужно
 * ровно один раз, при первом вызове ac97_play_test_tone). Проверено
 * отдельно: даёт ~2.4% отклонения от идеальной синусоиды — стандартное,
 * не слышимое ухом качество для табличного синтеза. */
#define SINE_TABLE_SIZE 256
static int16_t sine_table[SINE_TABLE_SIZE];
static int sine_table_ready = 0;

/* Вычисляет sin(x) для x из диапазона [-pi, pi] через ряд Тейлора с
 * 6 членами (x - x^3/3! + x^5/5! - x^7/7! + x^9/9! - x^11/11!).
 * Точность проверена отдельно на хосте: максимальная ошибка ~0.00045
 * на всём диапазоне [-pi, pi] — этого с большим запасом достаточно,
 * т.к. таблица потом дополнительно квантуется до 256 точек и
 * 16-битных сэмплов, где точность самого ряда уже не имеет значения.
 * Столько членов ряда можно себе позволить именно потому, что функция
 * считается только SINE_TABLE_SIZE (256) раз при старте, а не на
 * каждый сэмпл звука (сэмплов в буфере может быть десятки тысяч). */
static double taylor_sin(double x) {
    double x2 = x * x;
    double term = x;
    double sum = x;
    for (int k = 1; k <= 5; k++) {
        term *= -x2 / (double)((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sum;
}

static void ac97_ensure_sine_table(void) {
    if (sine_table_ready)
        return;

    const double pi = 3.14159265358979323846;
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        double angle = (2.0 * pi * i) / SINE_TABLE_SIZE;
        /* Приводим угол к диапазону [-pi, pi], где ряд Тейлора точен —
         * за его пределами ряд быстро "расходится" и даёт большую
         * ошибку (именно в этом и была исходная проблема: раньше угол
         * считался БЕЗ такого приведения, напрямую как позиция периода
         * от -pi до pi, что вроде бы то же самое, но при неточном
         * общем масштабировании давало ошибку до 50% на краях —
         * проверено и найдено тестом на хосте перед этим исправлением). */
        double x = angle;
        if (x > pi)
            x -= 2.0 * pi;

        double s = taylor_sin(x);
        if (s > 1.0) s = 1.0;   /* подстраховка от крохотного вылезания за границы из-за погрешности ряда */
        if (s < -1.0) s = -1.0;

        sine_table[i] = (int16_t)(s * 8000.0); /* амплитуда не на максимум int16 — чтобы не хрипело */
    }

    sine_table_ready = 1;
}

void ac97_play_test_tone(uint32_t freq_hz, uint32_t duration_ms) {
    if (!present)
        return;
    if (freq_hz == 0) freq_hz = 440;

    const uint32_t sample_rate = 44100;
    uint32_t frame_count = (sample_rate * duration_ms) / 1000; /* стерео-фреймов */
    if (frame_count == 0)
        return;

    int16_t* buf = (int16_t*) kmalloc(frame_count * 2 * sizeof(int16_t));
    if (!buf) {
        terminal_writestring("ac97: out of memory for test tone buffer\n");
        return;
    }

    /* Генерируем синусоиду через таблицу значений (lookup table) —
     * без стандартной <math.h> (которой в freestanding-окружении всё
     * равно нет). Таблица считается один раз при первом вызове.
     *
     * ВАЖНО: раньше здесь было приближение через ряд Тейлора
     * (sin(x) ~= x - x^3/6 + x^5/120), но при проверке на хосте
     * выяснилось, что на краях диапазона [-pi, pi] такое приближение
     * даёт погрешность до 0.52 при амплитуде синуса 1.0 (то есть до
     * ~50% от максимума!) — на слух это была бы заметно хрипящая,
     * искажённая волна вместо чистого тона. Таблица значений даёт
     * куда меньшую погрешность (~2.5% от амплитуды, чисто из-за
     * квантования на 256 отсчётов) и физически не может "разъехаться"
     * так сильно, как усечённый ряд. */
    ac97_ensure_sine_table();

    /* Position в таблице считаем инкрементально (не через прямое
     * умножение i * freq_hz * SINE_TABLE_SIZE — при длинном звуке i
     * может дорасти до сотен тысяч, и такое произведение переполнило
     * бы 32 бита; 64-битное деление здесь не годится, т.к. на этой
     * платформе оно требует функцию __udivdi3 из libgcc, которой у
     * нас нет без кросс-компилятора — see Makefile, комментарий про
     * "cannot find -lgcc"). Вместо этого просто прибавляем шаг фазы
     * на каждый сэмпл и не даём накопленному значению расти
     * неограниченно — сразу "сворачиваем" его в диапазон таблицы. */
    uint32_t phase_accum = 0; /* растёт на step каждый сэмпл, "оборачивается" по модулю */

    for (uint32_t i = 0; i < frame_count; i++) {
        uint32_t table_pos = (phase_accum * SINE_TABLE_SIZE) / sample_rate;
        int16_t sample = sine_table[table_pos % SINE_TABLE_SIZE];

        buf[i * 2]     = sample; /* left */
        buf[i * 2 + 1] = sample; /* right */

        phase_accum += freq_hz;
        if (phase_accum >= sample_rate)
            phase_accum -= sample_rate; /* держим accum < sample_rate, чтобы избежать переполнения на длинных звуках */
    }

    ac97_play(buf, frame_count * 2);
    kfree(buf);
}
