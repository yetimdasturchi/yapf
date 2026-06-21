<?php

$stdin = fopen('php://stdin', 'r');
if ($stdin === false) {
    fwrite(STDERR, "Cannot open stdin.\n");
    exit(1);
}

$originalTty = trim((string) shell_exec('stty -g 2>/dev/null'));
if ($originalTty !== '') {
    system('stty cbreak -echo');
    register_shutdown_function(static function () use ($originalTty): void {
        system('stty ' . escapeshellarg($originalTty) . ' 2>/dev/null');
    });
}

stream_set_blocking($stdin, false);

function translateKeypress(string $value): string
{
    return match ($value) {
        "\033[A" => 'UP',
        "\033[B" => 'DOWN',
        "\033[C" => 'RIGHT',
        "\033[D" => 'LEFT',
        "\n", "\r" => 'ENTER',
        ' ' => 'SPACE',
        "\010", "\177" => 'BACKSPACE',
        "\t" => 'TAB',
        "\e" => 'ESC',
        default => $value,
    };
}

echo "Press keys to test stdin. Press q or ESC to exit.\n";

while (true) {
    $keypress = fread($stdin, 16);
    if ($keypress === false || $keypress === '') {
        usleep(20000);
        continue;
    }

    $translated = translateKeypress($keypress);
    echo $translated . " pressed\n";

    if ($keypress === 'q' || $translated === 'ESC') {
        break;
    }
}
