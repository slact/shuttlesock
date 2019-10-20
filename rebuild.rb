#!/usr/bin/env ruby

ANALYZE_FLAGS=['--use-cc=clang', '-maxloop', '100']
ANALYZE_ALPHA_CHECKERS=[:core, :deadcode, :security, :unix]

def red(str) "\e[1;31m#{str}\e[1;0m" end
def green(str) "\e[1;32m#{str}\e[1;0m" end
def yellow(str) "\e[1;33m#{str}\e[1;0m" end
def blue(str) "\e[1;34m#{str}\e[1;0m" end

BASE_DIR=Dir.pwd
BUILD_DIR= "build"
raise "just a single-level build dir, please" if BUILD_DIR.match("/")
class Opts
  class Opt
    attr_accessor :name, :type, :opt, :matches
    def initialize(name, type, opt)
      @opt = opt
      @name = name.to_s.gsub("_", "-")
      @type = type
      @matches = {}
      @opt[:alt]=(opt[:alt] || {}).map{|v| v.to_s.gsub("_", "-")}
      @opt[:imply]=(opt[:imply]).map{|v| v.to_s.gsub("_", "-")} if @opt[:imply]
    end
    def method_missing(m, *arg)
      if m.match(/(.*)=$/)
        @opt[$1.to_sym]=arg[0]
      else
        @opt[m.to_sym]
      end
    end
  end
  
  class OptsDSL
    def initialize(opts)
      @opts = opts
    end
    def method_missing(*arg)
      @opts.define_option(*arg)
    end
  end
  
  attr_accessor :ok
  
  def initialize(&block)
    @arg_processed={}
    @opts = {}
    @analyze_flags = ANALYZE_FLAGS
    ANALYZE_ALPHA_CHECKERS.each {|c|@analyze_flags += ["-enable-checker", "alpha.#{c}"]}
    @cmake_defines = {}
    @cmake_opts = []
    @vars={}
    @exports={}
    @build_type = "Debug"
    OptsDSL.new(self).instance_eval &block
    
    @ok = process && generate_configure_script && configure && make && run
    if @vars[:clean_after]
      system "rm -Rf #{BUILD_DIR}"
    end
  end
  def define_option(name, type, opt={})
    @opts[name]=Opt.new(name, type, opt)
  end
  
  def generate_configure_script
    self
  end
  
  def opthelp
    h = []
    @opts.values.each do |opt|
      h << "  " + (([opt.display_as || opt.name] + (opt.alt || [])).join " ")
      h << "      (#{opt.info})" if opt.info
    end
    return h.join "\n"
  end
  
  def process_arg(arg)
    arg = arg.gsub("_", "-")
    return self if @arg_processed[arg.to_sym]
    @arg_processed[arg.to_sym] = true
    found= nil
    @opts.each do |k, opt|
      opt.matches= arg.match(opt.match || opt.name.to_s) || (opt.alt.member?(arg) && arg)
      if opt.matches then
        found = opt
        opt.run.call(opt, arg) if opt.run
        @analyze_flags += opt.analyze_flags || []
        @cmake_defines.merge!(opt.cmake_define || {})
        @cmake_opts += (opt.cmake_opts || [])
        @vars.merge!(opt.set || {})
        @exports.merge!(opt.export || {})
        @build_type = opt.build || @build_type
        (opt.imply || []).each do |implied_opt|
          process_arg implied_opt
        end
      end
    end
    if not found
      return false
    end
    return found
  end
  
  def process
    ARGV.each do |arg|
      if not process_arg arg
        $stderr.puts "invalid arg #{arg}. valid args are: \n #{opthelp}"
        return false
      end
    end
    
    if @vars[:help]
      puts "usage: ./rebuild.rb [args]"
      puts "possible arguments are:"
      puts opthelp
      return false
    end
    @exports.each do |n, v|
      ENV[n.to_s]= v==true ? "1" : (v == "false" ? "" : v.to_s)
    end
    
    @cmake_defines = {
      CMAKE_BUILD_TYPE: @build_type,
      SHUTTLESOCK_DEBUG_NO_WORKER_THREADS: false,
      SHUTTLESOCK_DEBUG_MODULE_SYSTEM: false,
      SHUTTLESOCK_STALLOC_TRACK_SPACE: false,
      SHUTTLESOCK_DEBUG_VALGRIND: false,
      SHUTTLESOCK_DEBUG_SANITIZE: false,
      DISABLE_CCACHE: false
    }.merge(@cmake_defines)
    
    exp={}
    @exports.each{|k,v| exp[k.to_s]=v}
    @exports=exp
    self
  end
  
  def env
    str=@exports.collect{|k,v| "#{k}=#{v}"}.join(" ")
    str+=" " if str.length > 0
    str
  end
  
  def configure
    # we need this stupid hack because cmake the idiot forgets its command-line defines if
    #  CMAKE_C_COMPILER is changed on a pre-existing build
    @last_used_compiler_file="#{BUILD_DIR}/.last_used_compiler.because_cmake_is_terrible"
    if File.exists?(@last_used_compiler_file) && (@vars[:compiler] || "") != File.read(@last_used_compiler_file)
      puts yellow ">> cmake build must be reset because a different compiler than"
      puts yellow "initially configured This is because cmake is utterly terrible."
      puts ""
      @vars[:clean]=true
    end
    
    if @vars[:clean]
      puts yellow ">> rm -Rf .#{BUILD_DIR}"
      system "rm", "-R", "-f", "#{BUILD_DIR}"
    end
    
    if !Dir.exists? BUILD_DIR
      system "rm", "-Rf", BUILD_DIR if File.exists? BUILD_DIR
      puts yellow ">> mkdir -p #{BUILD_DIR}"
      system "mkdir", "-p", BUILD_DIR
    end
    File.write(@last_used_compiler_file, @vars[:compiler])
    
    @def_opts = @cmake_defines.collect {|k,v| "-D#{k}=#{v == true ? "YES" : (v == false ? "NO" : v.to_s)}"}
    
    puts ""
    if `cmake --help`.match(/ -B /)
      ##build-path option exists
      @cmake_opts << "-B#{BUILD_DIR}"
    else
      shitty_cmake=true
      @cmake_opts << "../"
    end
    
    in_dir(shitty_cmake ? :build : :base) do
      if @vars[:clang_analyze]
        @scan_build=["scan-build"] + @analyze_flags
        @scan_build_view=@scan_build + ["--view"]
        puts yellow(">> #{env}") + blue(@scan_build.join " ") + yellow(" cmake \\")
      else
        puts yellow(">> #{env}cmake \\")
      end
      
      
      @def_opts.each { |d| puts green " #{d} \\" }
      puts @cmake_opts.map{ |o| " #{yellow(o)}"}.join(" \\\n")
      configure_command= (@scan_build || []) + ["cmake"] +  @def_opts + @cmake_opts
      @configure_result = system @exports, *configure_command
    end
    if !@configure_result
      $stderr.puts  red "cmake configuration step failed"
      return false
    end
    self
  end
  
  def make
    return self if @vars[:no_build]
    
    build_opts=[]
    if @vars[:verbose_build]
      if (`cmake --build 2>&1`).match("--verbose")
        build_opts << "--verbose"
      else
        @makefile_build = true
        build_opts << "VERBOSE=1"
      end
    end
    
    make_command = (@makefile_build ? ['make'] : ['cmake', '--build', BUILD_DIR]) + build_opts
    in_dir(@makefile_build ? :build : :base) do
      if @vars[:clang_analyze]
        puts yellow(">> ") + blue(@scan_build.join " ") + " " + yellow(make_command.join " ")
        begin
          @make_result= system @exports, *(@scan_build_view + make_command)
        rescue SignalException => e
          #it's ok
        end
      else
        puts yellow ">> #{make_command.join " "}"
        @make_result= system @exports, *(make_command)
      end
    end
    if !@make_result
      $stderr.puts  red "cmake build step failed"
      return false
    end
    @make_result && self
  end
  
  def system_echo(*args)
    echo = args.dup
    if Hash === echo[0]
      echo.shift
    end
    puts yellow ">> #{echo.join " "}"
    return system *args
  end
  
  def in_dir(what, opt = nil, &block)
    if what.to_sym == :build
      @in_build_dir = true
      puts yellow ">> cd #{BUILD_DIR}" unless opt == :quiet
      Dir.chdir BUILD_DIR
      yield
      puts yellow ">> cd .." unless opt == :quiet
      Dir.chdir BASE_DIR
    elsif what == :base
      prev = Dir.pwd
      Dir.chdir BASE_DIR
      yield
      Dir.chdir prev
    end
  end
  
  def run
    return self unless @vars[:run_test]
    puts green "Running tests..."
    in_dir :build do
      @run_ok = system './shuso_test', '--data-path=../test/data'
    end
    if @run_ok
      puts green "Tests passed"
    else
      $stderr.puts red "...tests faled"
      return false
    end

    if @build_type == "DebugCoverage"
      system "mkdir coverage 2>/dev/null"
      if File.exists? 'build/luacov.stats.out' then
        puts green "Preparing Lua coverage reports..."
        system "rm -Rf coverage/lua 2>/dev/null"
        system "mkdir coverage/lua 2>/dev/null"
        
        if system_echo 'luacov'
          puts green "done"
        else
          $stderr.puts red "luacov failed"
        end
      else
        puts green "No luacov.stats.out, skipping Lua coverage"
      end
      
      if !@vars[:no_display_coverage]
        puts green "Preparing C coverage results..."
        system "rm -Rf coverage/c 2>/dev/null"
        system "mkdir coverage/c 2>/dev/null"
        
        ok = false
        in_dir "build", :quiet do
          if @vars[:compiler] == "gcc"
            system 'mkdir coverage-report 2>/dev/null'
            ok = system_echo 'gcovr --root ../src --html-details -o ../coverage/c/index.html --gcov-ignore-parse-errors ./'
          elsif @vars[:compiler] == "clang"
            system_echo 'llvm-profdata merge -sparse *.profraw -o .profdata'
            ok = system_echo 'llvm-cov show -format="html" -output-dir="../coverage/c/" -instr-profile=".profdata"  -ignore-filename-regex="test/.*" -ignore-filename-regex="lib/.*" "libshuttlesock.so" -object "shuso_test"'
          else
            $stderr.puts red "don't know how to generate coverage reports with this compiler"
          end
        end
        if ok then
          puts green "done"
        else
          $stderr.puts red "failed"
        end
        if File.exist? 'coverage/lua/index.html'
          system 'xdg-open', 'coverage/lua/index.html'
        end
        system 'xdg-open', 'coverage/c/index.html'
      end
    end
    self
  end
end

rebuild = Opts.new do
  clang :debug_flag,
    export: { CC:"clang"},
    set: {compiler: "clang"}

  sanitize :debug_flag, alt: ["clang-sanitize", "sanitize-memory"],
    build: "DebugMSan",
    imply: [:clang]
  
  sanitize_threads :debug_flag, alt: ["sanitize-thread"],
    build: "DebugTSan",
    imply: [:clang]
  
  no_ccache :debug_flag,
    cmake_define: {DISABLE_CCACHE: true}
  
  no_build :debug_flag,
    set:{no_build: true}
  
  clean :debug_flag,
    set: {clean: true}
  
  libev_static :debug_flag,
    cmake_define: {LIBEV_STATIC: 1}
  
  c_ares_static :debug_flag,
    cmake_define: {C_ARES_BUILD_STATIC: 1}
  
  gcc :debug_flag,
    display_as: "gcc gcc5 gcc6 ...",
    match: (/gcc-?(\d?)/),
    run: (Proc.new do |opt, arg|
      num = opt.matches[1]
      if num && num.length > 0
        gcc="gcc-#{num}"
      else
        gcc="gcc"
      end
      opt.export= {CC: gcc}
      opt.set= {compiler: gcc}
    end)
  
  O :debug_flag, 
    display_as: "O0 O1 O2 O3",
    info: "compiler optimization flag",
    match: /O([0123sg])/,
    run: (Proc.new do |opt, arg|
      opt.cmake_define= {OPTIMIZE_LEVEL: opt.matches[1]}
    end)
  
  analyzer :debug_flag, alt: ["clang-analyzer", "scan", "analyze"],
    imply: [:clean, :no_ccache],
    set: {clean_after: true, clang_analyze: true}
  
  valgrind :debug_flag,
    cmake_define: {SHUTTLESOCK_DEBUG_VALGRIND: true}
  
  stalloc_track_space :debug_flag,
    cmake_define: {SHUTTLESOCK_STALLOC_TRACK_SPACE: true}
  
  sanitize :debug_flag,
    alt: ['clang-sanitize', 'sanitize-memory'],
    info: 'build with the clang memory sanitizer',
    build: 'DebugMSan',
    imply: [:clang],
    cmake_define: {SHUTTLESOCK_DEBUG_SANITIZE: true}
  
  sanitize_address :debug_flag,
    info: 'build with the clang address sanitizer',
    build: 'DebugASan',
    imply: [:clang],
    cmake_define: {SHUTTLESOCK_DEBUG_SANITIZE: true}
  
  verbose :flag, alt: [:v],
    set: {verbose_build: true}
  
  coverage :debug_flag,
    alt: ["clang_coverage"],
    imply: [:clang, :test, :luacov],
    build: "DebugCoverage"
  
  gcc_coverage :debug_flag,
    imply: [:gcc, :test, :luacov],
    build: "DebugCoverage"
  
  no_display_coverage :debug_flag,
    set: {no_display_coverage: true}
   
  release :debug_flag,
    build: "Release"
  
  release_debug :debug_flag,
    build: "RelWithDebInfo"
  
  self.test :debug_flag,
    set: {run_test: true}
  
  no_luac :debug_flag,
    cmake_define: {SHUTTLESOCK_NO_LUAC: true}
  
  nothread :debug_flag,
    alt: ["nothreads", "no-threads", "no-thread"],
    cmake_define: {SHUTTLESOCK_DEBUG_NO_WORKER_THREADS: true}
  
  debug_modules :debug_flag,
    cmake_define: {SHUTTLESOCK_DEBUG_MODULE_SYSTEM: true}
  
  no_eventfd :debug_flag,
    cmake_define: {SHUTTLESOCK_USE_EVENTFD: false}
  
  no_pool :debug_flag,
    alt: ["nopool"],
    cmake_define: {SHUTTLESOCK_DEBUG_STALLOC_NOPOOL: true}

  luacov :debug_flag,
    alt: ["lua_coverage"],
    imply: [:clean],
    cmake_define: {SHUTTLESOCK_DEBUG_LUACOV: true}
  
  cmake_debug :debug_flag,
    cmake_opts: ["--debug-output"]
  
  cmake_trace_expand :debug_flag,
    cmake_opts: ["--trace-expand"]
  
  include_what_you_use :debug_flag,
    cmake_define: {CMAKE_C_INCLUDE_WHAT_YOU_USE: "/usr/bin/include-what-you-use;-Xiwyu;--verbose=1"}
  
  passthrough :debug_flag,
    display_as: "-...",
    info: "passthrough configure option directly to cmake",
    match: /^(\-.+)/,
    repeatable: true,
    run: (Proc.new do |opt, arg|
      opt.cmake_opts=[arg]
    end)
  
  help :flag,
    set: {help: true}
    
end

exit 1 if not rebuild.ok
