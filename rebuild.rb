#!/usr/bin/env ruby

ANALYZE_FLAGS=['--use-cc=clang', '-maxloop', '100']
ANALYZE_ALPHA_CHECKERS=[:core, :deadcode, :security, :unix]

def red(str) "\e[1;31m#{str}\e[1;0m" end
def green(str) "\e[1;32m#{str}\e[1;0m" end
def yellow(str) "\e[1;33m#{str}\e[1;0m" end
def blue(str) "\e[1;34m#{str}\e[1;0m" end

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
    
    process && generate_configure_script && configure && make && run
    if @vars[:clean_after]
      system "rm -Rf ./build"
    end
  end
  def method_missing(name, type, opts)
    define_option name, type, opts
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
    #arg.gsub!("_", "-")
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
      SHUTTLESOCK_STALLOC_TRACK_SPACE: false,
      SHUTTLESOCK_VALGRIND: false,
      SHUTTLESOCK_SANITIZE: false,
      DISABLE_CCACHE: false
    }.merge(@cmake_defines)
    
    exp={}
    @exports.each{|k,v| exp[k.to_s]=v}
    @exports=exp
    self
  end
  
  def env
    @exports.collect{|k,v| "#{k}=#{v}"}.join(" ")
  end
  
  def configure
    # we need this stupid hack because cmake the idiot forgets its command-line defines if
    #  CMAKE_C_COMPILER is changed on a pre-existing build
    @last_used_compiler_file="./build/.last_used_compiler.because_cmake_is_terrible"
    if File.exists?(@last_used_compiler_file) && @vars[:compiler] != File.read(@last_used_compiler_file)
      puts yellow ">> cmake build must be reset because a different compiler than"
      puts yellow "initially configured This is because cmake is utterly terrible."
      puts ""
      @vars[:clean]=true
    end
    
    if @vars[:clean]
      puts yellow ">> rm -Rf ./build"
      system "rm", "-R", "-f", "./build"
    end
    
    if !File.exists? "build"
      puts yellow ">> mkdir build"
      system "mkdir", "-p", "build"
    end
    File.write(@last_used_compiler_file, @vars[:compiler])
    
    @def_opts = @cmake_defines.collect {|k,v| "-D#{k}=#{v == true ? "YES" : (v == false ? "NO" : v.to_s)}"}
    
    puts ""
    if `cmake --help`.match(/ -B /)
      ##build-path option exists
      @cmake_opts << "-B./build"
    else
      shitty_cmake=true
      puts yellow ">> cd ./build"
      Dir.chdir "build"
      @cmake_opts << "../"
    end
    
    if @vars[:clang_analyze]
      @scan_build=["scan-build"] + @analyze_flags
      @scan_build_view=@scan_build + ["--view"]
      puts yellow(">> #{env} ") + blue(@scan_build.join " ") + yellow(" cmake \\")
    else
      puts yellow(">> #{env} cmake \\")
    end
    
    
    @def_opts.each { |d| puts green " #{d} \\" }
    puts @cmake_opts.map{ |o| " #{yellow(o)}"}.join(" \\\n")
    configure_command= (@scan_build || []) + ["cmake"] +  @def_opts + @cmake_opts
    @configure_result = system @exports, *configure_command
    if shitty_cmake
      puts yellow">> cd ${base_dir}"
      Dir.chdir $base_dir
    end
    if !@configure_result
      $stderr.puts  red("cmake configuration step failed")
      return false
    end
    self
  end
  
  def make
    build_opts=[]
    if @vars[:verbose_build]
      if (`cmake --build 2>&1`).match("--verbose")
        build_opts << "--verbose"
      else
        @makefile_build = true
        build_opts << "VERBOSE=1"
      end
    end
    
    if @makefile_build
      puts yellow ">> cd ./build"
      Dir.chdir "build"
      make_command = ["make"] + build_opts
    else
      make_command = ["cmake", "--build", "./build"] + build_opts
    end

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
    @make_result && self
  end
  
  def run
    return self unless @vars[:run_test]
    Dir.chdir "./build"
    puts green "Running tests..."
    if !system './shuso_test'
      $stderr.puts red "...tests faled"
      return false
    end
    Dir.chdir ".."
    
    if @build_type == "DebugCoverage" && !@vars[:no_display_coverage]
      puts green "Preparing coverage results..."
      Dir.chdir "./build" 
      if @vars[:compiler] == "gcc"
        system 'mkdir coverage-report 2>/dev/null'
        system 'gcovr --root ../src --html-details -o coverage-report/index.html --gcov-ignore-parse-errors ./'
        puts green "done"
      elsif @vars[:compiler] == "clang"
        system 'llvm-profdata merge -sparse *.profraw -o .profdata'
        system 'llvm-cov show -format="html" -output-dir="coverage-report" -instr-profile=".profdata"  -ignore-filename-regex="test/.*" -ignore-filename-regex="lib/.*" "libshuttlesock.so" -object "shuso_test"'
        puts green "done"
      else
        $stderr.puts red "don't know how to generate coverage reports for this compiler"
      end
      system 'xdg-open', './coverage-report/index.html'
      Dir.chdir ".."
    end
  end
end

Opts.new do
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
  
  clean :debug_flag,
    set: {clean: true}
  
  c_ares_static :debug_flag,
    cmake_define: {C_ARES_BUILD_STATIC: 1}
  
  gcc :debug_flag,
    display_as: "gcc gcc5 gcc6 ...",
    match: (/gcc(\d?)/),
    run: (Proc.new do |opt, arg|
      opt.export= {CC: "gcc#{opt.matches[1]}"}
      opt.set= {compiler: "gcc#{opt.matches[1]}"}
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
    cmake_define: {SHUTTLESOCK_VALGRIND: true}
  
  stalloc_track_space :debug_flag,
    cmake_define: {SHUTTLESOCK_STALLOC_TRACK_SPACE: true}
  
  sanitize :debug_flag,
    alt: ['clang-sanitize', 'sanitize-memory'],
    info: 'build with the clang memory sanitizer',
    build: 'DebugMSan',
    imply: [:clang],
    cmake_define: {SHUTTLESOCK_SANITIZE: true}
  
  sanitize_address :debug_flag,
    info: 'build with the clang address sanitizer',
    build: 'DebugASan',
    imply: [:clang],
    cmake_define: {SHUTTLESOCK_SANITIZE: true}
  
  verbose :flag, alt: [:v],
    set: {verbose_build: true}
  
  coverage :debug_flag,
    alt: ["clang_coverage"],
    imply: [:clang, :test],
    build: "DebugCoverage"
  
  gcc_coverage :debug_flag,
    imply: [:gcc, :test],
    build: "DebugCoverage"
  
  no_display_coverage :debug_flag,
    set: {no_display_coverage: true}
   
  release :debug_flag,
    build: "Release"
  
  release_debug :debug_flag,
    build: "RelWithDebInfo"
  
  self.test :debug_flag,
    set: {run_test: true}
  
  nothread :debug_flag,
    alt: ["nothreads", "no-threads"],
    cmake_define: {SHUTTLESOCK_DEBUG_NO_WORKER_THREADS: true}
  
  no_eventfd :debug_flag,
    cmake_define: {SHUTTLESOCK_USE_EVENTFD: false}
  
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

