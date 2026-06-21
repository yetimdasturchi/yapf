<?php

namespace YapfExample\App;

final class Application
{
    public function run(): void
    {
        global $argv;

        $storage = getenv('APP_STORAGE') ?: null;
        $writeOk = false;

        if ($storage) {
            $writeOk = file_put_contents($storage . '/example.log', date(DATE_ATOM) . ' YAPF storage test' . PHP_EOL, FILE_APPEND) !== false;
        }

        $payload = [
            'app' => 'YAPF example via composer-like autoload',
            'time' => date(DATE_ATOM),
            'php' => PHP_VERSION,
            'args' => array_values(array_slice($argv ?? [], 1)),
            'env' => getenv('APP_ENV') ?: 'not-set',
            'storage' => $storage,
            'storage_write' => $writeOk,
            'class' => self::class,
            'src_is_dir' => is_dir(__DIR__ . '/..'),
            'autoload_is_file' => is_file(__DIR__ . '/../../vendor/autoload.php'),
            'src_entries' => array_values(array_diff(scandir(__DIR__ . '/..'), ['.', '..'])),
            'root_entries' => array_values(array_diff(scandir(__DIR__ . '/../..'), ['.', '..'])),
        ];

        echo json_encode($payload, JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES) . PHP_EOL;
    }
}
