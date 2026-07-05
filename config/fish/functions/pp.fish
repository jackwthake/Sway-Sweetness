function pp
    set dir (proj-pickr $argv)
    if test -n "$dir"
        cd $dir
    end
end
