# bash completion for memreduct

_memreduct ()
{
	local cur prev commands opts
	cur="${COMP_WORDS[COMP_CWORD]}"
	prev="${COMP_WORDS[COMP_CWORD-1]}"

	commands="status clean top monitor daemon"
	opts="--pagecache --dentries --compact --swap --all
		--threshold --interval --refresh --limit --json --config --quiet
		--help --version"

	case "$prev" in
		-c|--config)
			COMPREPLY=( $(compgen -f -- "$cur") )
			return
			;;
		-t|--threshold|-n|--interval|-i|--refresh|-l|--limit)
			return
			;;
	esac

	if [ "$COMP_CWORD" -eq 1 ]; then
		COMPREPLY=( $(compgen -W "$commands $opts" -- "$cur") )
	else
		COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
	fi
}

complete -F _memreduct memreduct
