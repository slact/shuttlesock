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
    attr_accessor :name, :type, :opt, :matches, :priority
    def initialize(name, type, opt)
      @opt = opt
      @name = name.to_s.gsub("_", "-")
      @type = type
      @matches = {}
      @priority = opt[:priority] || 0
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
    def DEFINE_OPTION(*arg)
      @opts.define_option(*arg)
    end
    
    def method_missing(*arg)
      DEFINE_OPTION(*arg)
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
    @opts.each do |optname, opt|
      if opt.cmake_define
        opt.cmake_define.each do |k, v|
          if not @cmake_defines[k] then
            @cmake_defines[k]=nil
          end
        end
      end
    end
    
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
      if opt.match then
        opt.matches= arg.match(opt.match)
      elsif opt.name.to_s == arg || (opt.alt.member?(arg) && arg)
        opt.matches=[arg]
      else
        opt.matches=false
      end
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
        break
      end
    end
    @cmake_defines = Hash[@cmake_defines.sort_by {|k,v| k.to_s}]
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
    if File.exist?(@last_used_compiler_file) && (@vars[:compiler] || "") != File.read(@last_used_compiler_file)
      puts yellow ">> cmake build must be reset because a different compiler than"
      puts yellow "initially configured This is because cmake is utterly terrible."
      puts ""
      @vars[:clean]=true
    end
    
    if @vars[:clean]
      puts yellow ">> rm -Rf .#{BUILD_DIR}"
      system "rm", "-R", "-f", "#{BUILD_DIR}"
    end
    
    if !Dir.exist? BUILD_DIR
      system "rm", "-Rf", BUILD_DIR if File.exist? BUILD_DIR
      puts yellow ">> mkdir -p #{BUILD_DIR}"
      system "mkdir", "-p", BUILD_DIR
    end
    File.write(@last_used_compiler_file, @vars[:compiler])
    
    @def_opts = @cmake_defines.collect do |k,v|
      if v == true  then
        "-D#{k}=YES"
      elsif v == false then
        "-D#{k}=NO"
      elsif v == nil then
        "-D#{k}="
      else
        "-D#{k}=#{v.to_s}"
      end
    end
    
    puts ""
    if `cmake --help`.match(/ -B /)
      ##build-path option exists
      @cmake_opts << "-B#{BUILD_DIR}"
    else
      shitty_cmake=true
      @cmake_opts << "../"
    end
    
    if @vars[:generator] == "Ninja" || !@vars[:generator]
      if File.exist? "/usr/bin/ninja"
        @cmake_opts << "-GNinja"
        @ninja = true
      elsif @vars[:generator] == "Ninja"
        $stderr.puts  red "Ninja build system not found"
        return false
      end
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
    cmake_build_output = `cmake --build 2>&1`
    if @vars[:verbose_build]
      if (`cmake --build 2>&1`).match("--verbose")
        build_opts << "--verbose"
      else
        @direct_build = true
        build_opts << "VERBOSE=1"
      end
    end
    
    #if !@direct_build && !@ninja && cmake_build_output.match("--parallel")
    #  begin
    #    require 'etc'
    #    nprocs = Etc.nprocessors
    #    build_opts += ["--parallel", "#{nprocs}"]
    #  rescue Exception
    #  end
    #end
    
    make_command = (@direct_build ? [(@have_ninja ? 'ninja' : 'make')] : ['cmake', '--build', BUILD_DIR]) + build_opts
    in_dir(@direct_build ? :build : :base) do
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
  
  def fake_system_echo(*args)
    puts yellow ">> #{args.join " "}"
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
      test_selector = @vars[:test_selector] ? "\"#{@vars[:test_selector]}\"" : ""
      @run_ok = system_echo "./shuso_test --data-path=\"../test/data\" #{test_selector}#{@vars[:verbose_test] ? " --verbose" : ""}"
    end
    if @run_ok
      puts green "Tests passed"
    else
      $stderr.puts red "...tests faled"
      return false unless @build_type == "DebugCoverage"
    end
    if @build_type == "DebugCoverage"
      statsfiles = Dir.glob("#{BUILD_DIR}/luacov.stats.out.*")
      fake_system_echo "./merge_luacov_stats.lua", "#{BUILD_DIR}/luacov.stats.out.*", "--out=#{BUILD_DIR}/luacov.stats.out"
      system "./merge_luacov_stats.lua", *statsfiles, "--out=#{BUILD_DIR}/luacov.stats.out"
      system_echo "mkdir coverage 2>/dev/null"
      if File.exist? "#{BUILD_DIR}/luacov.stats.out" then
        puts green "Preparing Lua coverage reports..."
        system_echo "rm -Rf coverage/lua 2>/dev/null"
        system_echo "mkdir coverage/lua 2>/dev/null"
        
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
        system_echo "rm -Rf coverage/c 2>/dev/null"
        system_echo "mkdir coverage/c 2>/dev/null"
        
        ok = false
        in_dir "build", :quiet do
          if @vars[:compiler] == "gcc"
            system_echo 'mkdir coverage-report 2>/dev/null'
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
          system_echo 'xdg-open', 'coverage/lua/index.html'
        end
        system_echo 'xdg-open', 'coverage/c/index.html'
      end
    end
    @run_ok && self
  end
end

rebuild = Opts.new do
  clang :debug_flag,
    export: { CC:"clang"},
    set: {compiler: "clang"}
  
  no_ccache :debug_flag,
    cmake_define: {DISABLE_CCACHE: true}
  
  no_build :debug_flag,
    set:{no_build: true}
  
  clean :debug_flag,
    set: {clean: true}
  
  gcc :debug_flag,
    display_as: "gcc gcc5 gcc6 ...",
    match: (/^gcc-?(\d?)$/),
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
    match: /^O([0123sg])/,
    cmake_define: {OPTIMIZE_LEVEL: ""},
    run: (Proc.new do |opt, arg|
      opt.cmake_define= {OPTIMIZE_LEVEL: opt.matches[1]}
    end)
  
  analyzer :debug_flag, alt: ["clang-analyzer", "scan", "analyze"],
    imply: [:clean, :no_ccache],
    set: {clean_after: true, clang_analyze: true}
  
  valgrind :debug_flag,
    cmake_define: {SHUTTLESOCK_DEBUG_VALGRIND: true}
  
  pool_track_space :debug_flag,
    cmake_define: {SHUTTLESOCK_POOL_TRACK_SPACE: true}
  
  sanitize :debug_flag,
    alt: ['clang-sanitize', 'sanitize-memory'],
    info: 'build with the clang memory sanitizer',
    build: 'DebugMemorySanitizer',
    imply: [:clang, :libs_static],
    cmake_define: {SHUTTLESOCK_DEBUG_SANITIZE: true}
  
  sanitize_threads :debug_flag, alt: ["sanitize-thread"],
    build: "DebugThreadSanitizer",
    imply: [:clang]
  
  sanitize_control_flow_integrity :debug_flag, alt: ["sanitize-cfi"],
    build: "DebugCFISanitizer",
    imply: [:clang]
  
  sanitize_address :debug_flag,
    info: 'build with the clang address sanitizer',
    build: 'DebugAddressSanitizer',
    imply: [:clang, :libs_static],
    cmake_define: {SHUTTLESOCK_DEBUG_SANITIZE: true}
  
  verbose :flag, alt: [:v],
    set: {verbose_build: true},
    cmake_define: {CMAKE_VERBOSE_MAKEFILE: true}
  
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
  
  DEFINE_OPTION(:test, :debug_flag,
    {
      set: {run_test: true}
    }
  )
  
  test_selector :debug_flag, 
    display_as: "test_selector=<selector>",
    info: "if running tests, run ones that match this selector",
    match: /^test-selector=(.+)/,
    run: (Proc.new do |opt, arg|
      opt.set= {test_selector: opt.matches[1]}
    end)
  
  test_verbose :debug_flag,
    set: {verbose_test: true}
  
  no_luac :debug_flag,
    cmake_define: {SHUTTLESOCK_NO_LUAC: true}
  
  nothread :debug_flag,
    alt: ["nothreads", "no-threads", "no-thread"],
    cmake_define: {SHUTTLESOCK_DEBUG_NO_WORKER_THREADS: true}
  
  debug_modules :debug_flag,
    cmake_define: {SHUTTLESOCK_DEBUG_MODULE_SYSTEM: true}
  
  debug_events :debug_flag,
    cmake_define: {SHUTTLESOCK_DEBUG_EVENTS: true}
  
  no_eventfd :debug_flag,
    cmake_define: {SHUTTLESOCK_USE_EVENTFD: false}
  
  no_io_uring :debug_flag,
    cmake_define: {SHUTTLESOCK_USE_IO_URING: false}
  
  no_pool :debug_flag,
    alt: ["nopool"],
    cmake_define: {SHUTTLESOCK_DEBUG_NOPOOL: true}

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
  
  lua_version :flag,
    display_as: "lua=...",
    info: "USe specified Lua version",
    match: /^lua=(.*)/,
    cmake_define: {LUA_VERSION: ""},
    run: (Proc.new do |opt, arg|
      opt.cmake_define= {LUA_VERSION: opt.matches[1]}
    end)
  
  lua_apicheck :debug_flag,
    imply: [:lua_static],
    cmake_define: {LUA_BUILD_STATIC_EXTRAFLAGS: "-DLUA_USE_APICHECK"}
  
  libs_static :debug_flag,
    imply: [:lua_static, :openssl_static, :liburing_static, :libev_static, :c_ares_static, :pcre_static]
  
  lua_static :debug_flag,
    cmake_define: {LUA_BUILD_STATIC: true}
  
  openssl_static :debug_flag,
    cmake_define: {OPENSSL_BUILD_STATIC: true}
  
  liburing_static :debug_flag,
    cmake_define: {LIBURING_BUILD_STATIC: true}
  
  libev_static :debug_flag,
    cmake_define: {LIBEV_BUILD_STATIC: 1}
  
  libev_verify :debug_flag,
    imply: [:libev_static],
    cmake_define: {LIBEV_BUILD_STATIC_EXTRAFLAGS: "-DEV_VERIFY=3"}
  
  c_ares_static :debug_flag,
    cmake_define: {C_ARES_BUILD_STATIC: 1}
  
  pcre_static :debug_flag,
    cmake_define: {PCRE_BUILD_STATIC: 1}
  
  makefile :debug_flag,
    set: {generator: "Unix Makefiles"}
  
  ninja :debug_flag,
    set: {generator: "Ninja"}
  
  help :flag,
    alt: ['-h', '--help'],
    set: {help: true}
  
  passthrough :debug_flag,
    display_as: "-...",
    info: "passthrough configure option directly to cmake",
    match: /^(\-.+)/,
    repeatable: true,
    run: (Proc.new do |opt, arg|
      opt.cmake_opts=[arg]
    end)
    
end

exit 1 if not rebuild.ok
