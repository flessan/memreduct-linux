# fish completion for memreduct

complete -c memreduct -f

complete -c memreduct -n __fish_use_subcommand -a status -d 'Show current memory usage'
complete -c memreduct -n __fish_use_subcommand -a clean -d 'Clean memory now (requires root)'
complete -c memreduct -n __fish_use_subcommand -a top -d 'Show processes using the most memory'
complete -c memreduct -n __fish_use_subcommand -a monitor -d 'Interactive live monitor'
complete -c memreduct -n __fish_use_subcommand -a daemon -d 'Run auto-clean daemon'

complete -c memreduct -l pagecache -d 'Drop clean page cache'
complete -c memreduct -l dentries -d 'Drop dentry/inode slab caches'
complete -c memreduct -l compact -d 'Compact physical memory'
complete -c memreduct -l swap -d 'Flush swap back to RAM'
complete -c memreduct -l all -d 'Clean all memory areas'

complete -c memreduct -s t -l threshold -x -d 'Auto-clean at N% usage'
complete -c memreduct -s n -l interval -x -d 'Auto-clean every N minutes'
complete -c memreduct -s i -l refresh -x -d 'Refresh/poll period in seconds'
complete -c memreduct -s l -l limit -x -d 'Number of processes to show'
complete -c memreduct -s j -l json -d 'JSON output'
complete -c memreduct -s c -l config -r -d 'Alternative config file'
complete -c memreduct -s q -l quiet -d 'Disable desktop notifications'
complete -c memreduct -s h -l help -d 'Show help'
complete -c memreduct -s v -l version -d 'Show version'
