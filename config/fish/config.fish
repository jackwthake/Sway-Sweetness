if status is-interactive
    # Commands to run in interactive sessions can go here
    fastfetch
    pp ~/Code
end

fish_add_path $HOME/.local/bin

# Go
set -gx GOPATH $HOME/go
fish_add_path /usr/local/go/bin
fish_add_path $GOPATH/bin


# BEGIN opam configuration
# This is useful if you're using opam as it adds:
#   - the correct directories to the PATH
#   - auto-completion for the opam binary
# This section can be safely removed at any time if needed.
test -r '/home/jwt/.opam/opam-init/init.fish' && source '/home/jwt/.opam/opam-init/init.fish' > /dev/null 2> /dev/null; or true
# END opam configuration
