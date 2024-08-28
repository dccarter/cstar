require 'set'
mem = { }
show=ARGV[1]

File.readlines(ARGV[0], chomp: true).each do |line|
    next if not line.start_with?(" mem(")
    if not line.index(')')
        puts line
        next
    end

    info = line[5..line.index(')')-1]
    parts = info.split(':')

    tag = parts[0].strip
    addr = parts[1].strip
    next if addr == "0x0"

    if not mem.key?(addr)
        mem[addr] = {
            :loc => line[line.index('@')+1, line.length-1],
            :refs => 0,
            :get => 0,
            :gets => Set[],
            :drop => 0,
            :drops => Set[],
            :freed => false,
            :tag => tag
        }
    end

    if tag == "alloc"
        mem[addr][:loc] = line[line.index('@')+1, line.length-1]
        mem[addr][:refs] = 1
    elsif tag == "drop"
        mem[addr][:drop] = mem[addr][:drop] + 1
        mem[addr][:drops].add(line[line.index('@')+1, line.length-1])
    elsif tag == "get"
        mem[addr][:get] = mem[addr][:get] + 1
        mem[addr][:gets].add(line[line.index('@')+1, line.length-1])
    elsif tag == "freed"
        mem[addr][:freed] = true
    end
end

mem.each { |addr, info|
    if not info[:freed]
        puts "#{addr} @ #{info[:loc]}"
        puts "    refs: #{info[:refs]}, drop: #{info[:drop]}, get: #{info[:get]}"
    end
}

if mem.key?(show)
    details = mem[show]
    puts "#{show} :: freed: #{details[:freed]}, get: #{details[:get]}, drop: #{details[:drop]}"
    puts "  alloc: #{details[:loc]}"
    puts "  Get"
    details[:gets].each{ |loc|
        puts "    #{loc}"
    }
    puts "  Drop"
    details[:drops].each{ |loc|
        puts "    #{loc}"
    }
end