#!/usr/bin/ruby
# coding: utf-8

$LOAD_PATH.push('metabuild/lib')
require 'metabuild'
include Metabuild
CONFIGS = `make list-configs`.split(" ").inject({}){|x, c| x.merge({ c => {} })}

APP_NAME = "ODP-perf"

options = Options.new({ "k1tools"       => [ENV["K1_TOOLCHAIN_DIR"].to_s,"Path to a valid compiler prefix."],
                        "artifacts"     => {"type" => "string", "default" => "", "help" => "Artifacts path given by Jenkins"},
                        "local-run"     => {"type" => "string", "default" => "0", "help" => "Run target locally"},
                        "debug"         => {"type" => "boolean", "default" => false, "help" => "Debug mode." },
                        "list-configs"  => {"type" => "boolean", "default" => false, "help" => "List all targets" },
                        "configs"       => {"type" => "string", "default" => CONFIGS.keys.join(" "), "help" => "Build configs. Default = #{CONFIGS.keys.join(" ")}" },
                        "valid-configs" => {"type" => "string", "default" => CONFIGS.keys.join(" "), "help" => "Build configs. Default = #{CONFIGS.keys.join(" ")}" },
                        "output-dir"    => [nil, "Output directory for RPMs."],
                        "k1version"     => ["unknown", "K1 Tools version required for building ODP applications"],
                        "librariesversion" => ["", "k1-libraries version required for building ODP applications"],
                     })

if options["list-configs"] == true then
    puts CONFIGS.map(){|n, i| n}.join("\n")
    exit 0
end
workspace  = options["workspace"]
odp_clone  = options['clone']
jobs = options['jobs']

local_run = options["local-run"]

odp_path   = File.join(workspace,odp_clone)

odp_perf_files_path = "#{odp_path}/perf_files"
odp_artifact_files_path = "#{odp_path}/artifact_files"

k1tools = options["k1tools"]

$env = {}
$env["K1_TOOLCHAIN_DIR"] = k1tools
$env["PATH"] = "#{k1tools}/bin:#{ENV["PATH"]}"
$env["LD_LIBRARY_PATH"] = "#{k1tools}/lib:#{k1tools}/lib64:#{ENV["LD_LIBRARY_PATH"]}"

repo = Git.new(odp_clone,workspace)

local_valid = options["local-valid"]

clean = Target.new("clean", repo, [])
changelog = Target.new("changelog", repo, [])
build = Target.new("build", repo, [changelog])
install = Target.new("install", repo, [build])
report_perf = Target.new("report-perf", repo, [])
valid = ParallelTarget.new("valid", repo, [install])
valid_packages = ParallelTarget.new("valid-packages", repo, [])

long = nil
apps = nil

long_build = Target.new("long-build", repo, [install])
apps = Target.new("apps", repo, [install])
long = Target.new("long", repo, [])
dkms = Target.new("dkms", repo, [])
package = Target.new("package", repo, [install, apps, long_build, dkms])


b = Builder.new("odp", options, [clean, changelog, build, valid, valid_packages,
                                 long_build, long, apps, dkms, package, install, report_perf])

b.logsession = "odp"

b.default_targets = [valid]

debug_flags = options["debug"] == true ? "--enable-debug" : ""

configs=nil
valid_configs = options["valid-configs"].split()
valid_type = "sim"

if ENV["label"].to_s() != "" then
    case ENV["label"]
    when /MPPADevelopers-ab01b*/, /MPPAEthDevelopers-ab01b*/
        valid_configs = [ "k1b-kalray-nodeos_developer", "k1b-kalray-mos_developer" ]
        valid_type = "jtag"
    when /KONIC80Developers*/, /MPPA_KONIC80_Developers*/
        valid_configs = [ "k1b-kalray-nodeos_konic80", "k1b-kalray-mos_konic80" ]
        valid_type = "jtag"
    when /MPPA_EMB01b_centos7-with-eth-loopback/
        valid_configs = [ "k1b-kalray-mos_emb01" ]
        valid_type = "remote"
    when /MPPA_AB04_Developers-with-loopback/
        valid_configs = [ "k1b-kalray-mos_ab04" ]
        valid_type = "jtag"
    when "fedora19-64","debian6-64", /MPPADevelopers*/, /MPPAEthDevelopers*/
        # Validate nothing.
        valid_configs = [ ]
    when "fedora17-64"
        configs= [ "k1b-kalray-nodeos_explorer", "k1b-kalray-mos_explorer" ]
        # Validate nothing.
        valid_configs = [ ]
    when "debian7-64"
	    configs = [ "k1b-kalray-mos_developer", "k1b-kalray-mos_konic80" ]
        # Validate nothing.
        valid_configs = [ ]
    when "centos7-64"
        valid_configs = "k1b-kalray-nodeos_simu", "k1b-kalray-mos_simu"
        valid_type = "sim"
    when /MPPAExplorers_k1b*/
        valid_configs = [ "k1b-kalray-nodeos_explorer", "k1b-kalray-mos_explorer" ]
        valid_type = "jtag"
    else
        raise("Unsupported label #{ENV["label"]}!")
    end
end

if configs == nil then
    configs = (options["configs"].split(" ")).uniq
    configs.each(){|conf|
        raise ("Invalid config '#{conf}'") if CONFIGS[conf] == nil
    }
end

if options["output-dir"] != nil then
    artifacts = File.expand_path(options["output-dir"])
else
    artifacts = File.join(workspace,"artifacts")
end
mkdir_p artifacts unless File.exists?(artifacts)

b.target("changelog") do
    b.logtitle = "Report for odp changelog"
    cd odp_path
    ref_commit = ENV["INTEGRATION_BRANCH"]
    if(ref_commit == nil || ref_commit == "")
        # Do not know what is the source... Try HEAD^
        ref_commit = "HEAD^"
    else
	ref_commit = "origin/#{ref_commit}"
    end
    b.run("./ls_modules.sh #{ref_commit} HEAD | ./ansi2html.sh > #{artifacts}/ls_modules.html")
end


b.target("build") do
    b.logtitle = "Report for odp build."
    cd odp_path
    b.run(:cmd => "make build CONFIGS='#{configs.join(" ")}'")
end

b.target("valid") do
    b.logtitle = "Report for odp tests."
    cd odp_path

    b.valid(:cmd => "make valid CONFIGS='#{valid_configs.join(" ")}'")

    b.valid(:cmd => "mkdir -p perf_files")
    #b.valid(:cmd => "mkdir -p artifact_files")

    if options['logtype'] == :junit then
        fName=File.dirname(options['logfile']) + "/" + "automake-tests.xml"
        b.valid(:cmd => "make junits CONFIGS='#{valid_configs.join(" ")}' JUNIT_FILE=#{fName}")
    end
end


b.target("long-build") do
    b.logtitle = "Report for odp tests."
    cd odp_path

    b.run(:cmd => "make long-install CONFIGS='#{configs.join(" ")}' LONG_CONFIGS='#{configs.join(" ")}'")
end

b.target("long") do
    b.logtitle = "Report for odp tests."
    cd odp_path

    valid_configs.each(){|conf|
        board=conf.split("_")[1]
        platform=conf.split("_")[0]

        testEnv = $env.merge({ :test_name => "long-#{conf}", :perf_files_path => odp_perf_files_path})

        if local_run == "1" then
            cd File.join(odp_path, "install/local/k1tools/share/odp/long", board, platform)
            ENV["LOCAL_RUN"] = "1"
        else
            cd File.join(ENV["K1_TOOLCHAIN_DIR"], "share/odp/long/", board, platform)
        end
        
        b.ctest( {
                     :ctest_args => "-L #{valid_type}",
                     :fail_msg => "Failed to validate #{conf}",
                     :success_msg => "Successfully validated #{conf}",
                     :env => testEnv,
                 })
    }
    b.report_perf_files("ODP-perf", [odp_perf_files_path])
end


b.target("valid-packages") do
    b.logtitle = "Report for odp tests."

    valid_configs.each(){|conf|
        board=conf.split("_")[1]
        platform=conf.split("_")[0]
        [ "platform/mppa/test", "test/performance", "helper/test"].each(){|dir|
            cd File.join(ENV["K1_TOOLCHAIN_DIR"], "share/odp/tests", board,
                         platform, dir)
            testEnv = $env.merge({ :test_name => "valid-#{conf}-#{dir}"})
           b.ctest( {
                         :ctest_args => "",
                         :fail_msg => "Failed to validate #{conf}",
                         :success_msg => "Successfully validated #{conf}",
                         :env => testEnv,
                     })
        }
    }
end

b.target("apps") do
    b.logtitle = "Report for odp apps."
    cd odp_path

    b.run(:cmd => "make apps-install")

end

b.target("install") do

    b.logtitle = "Report for odp install."
    cd odp_path

    b.run(:cmd => "rm -Rf install/")
    b.run(:cmd => "make install CONFIGS='#{configs.join(" ")}'")
end

b.target("package") do
    b.logtitle = "Report for odp package."
    cd odp_path

    b.run(:cmd => "cd install/; tar cf ../odp.tar local/k1tools/lib/ local/k1tools/share/odp/firmware local/k1tools/share/odp/build/ local/k1tools/share/odp/skel/ local/k1tools/k1*/include local/k1tools/share/doc/ local/k1tools/lib64", :env => $env)
    b.run(:cmd => "cd install/; tar cf ../odp-tests.tar local/k1tools/share/odp/tests local/k1tools/share/odp/long local/k1tools/share/odp/perf", :env => $env)
    b.run(:cmd => "cd install/; tar cf ../odp-apps-internal.tar local/k1tools/share/odp/apps", :env => $env)
    b.run(:cmd => "cd install/; tar cf ../odp-cunit.tar local/k1tools/kalray_internal/cunit", :env => $env)

    (version,releaseID,sha1) = repo.describe()
    release_info = b.release_info(version,releaseID,sha1)
    sha1 = repo.sha1()

    #K1 ODP
    tar_package = File.expand_path("odp.tar")
    depends = []
    depends.push b.depends_info_struct.new("k1-tools","=", options["k1version"], "")

    if not options["librariesversion"].to_s.empty? then
      depends.push b.depends_info_struct.new("k1-libraries","=", options["librariesversion"], "")
    end

    package_description = "K1 ODP package (k1-odp-#{version}-#{releaseID} sha1 #{sha1})."
    pinfo = b.package_info("k1-odp", release_info,
                           package_description,
                           depends, "/usr", workspace)
    b.create_package(tar_package, pinfo)

    #K1 ODP Tests
    tar_package = File.expand_path("odp-tests.tar")
    depends = []
    depends.push b.depends_info_struct.new("k1-odp","=", release_info.full_version)
    package_description = "K1 ODP Standard Tests (k1-odp-tests-#{version}-#{releaseID} sha1 #{sha1})."
    pinfo = b.package_info("k1-odp-tests", release_info,
                           package_description, 
                           depends, "/usr", workspace)
    b.create_package(tar_package, pinfo)

    #K1 ODP Apps Internal
    tar_package = File.expand_path("odp-apps-internal.tar")
    depends = []
    depends.push b.depends_info_struct.new("k1-odp","=", release_info.full_version)
    package_description = "K1 ODP Internal Application and Demo  (k1-odp-apps-internal-#{version}-#{releaseID} sha1 #{sha1})."
    pinfo = b.package_info("k1-odp-apps-internal", release_info,
                           package_description, 
                           depends, "/usr", workspace)
    b.create_package(tar_package, pinfo)

    #K1 ODP CUnit
    tar_package = File.expand_path("odp-cunit.tar")
    depends = []
    depends.push b.depends_info_struct.new("k1-odp-cunit","=", release_info.full_version)
    package_description = "K1 ODP CUnit (k1-odp-cunit-#{version}-#{releaseID} sha1 #{sha1})."
    pinfo = b.package_info("k1-odp-cunit", release_info,
                           package_description,
                           depends, "/usr", workspace)
    b.create_package(tar_package, pinfo)


    # Generates k1r_parameters.sh
    output_parameters = File.join(artifacts,"parameters.sh")
    b.run("rm -f #{output_parameters}")
    b.run("echo 'K1ODP_VERSION=#{version}-#{releaseID}' >> #{output_parameters}")
    b.run("echo 'K1ODP_RELEASE=#{version}' >> #{output_parameters}")
    b.run("echo 'K1ODP_REVISION=#{repo.long_sha1()}' >> #{output_parameters}")
    b.run("echo 'COMMITER_EMAIL=#{ENV.fetch("COMMITER_EMAIL",options["email"])}' >> #{output_parameters}")
    b.run("echo 'INTEGRATION_BRANCH=#{ENV.fetch("INTEGRATION_BRANCH",options["branch"])}' >> #{output_parameters}")
    b.run("echo 'TMP_BRANCH=#{ENV["TARGET_BRANCH"]}' >> #{output_parameters}")
    b.run("echo 'REVISION=#{repo.long_sha1()}' >> #{output_parameters}")
    b.run("echo 'INIC_BUILD_NUMBER=#{ENV["BUILD_NUMBER"]}' >> #{output_parameters}")
    b.run("#{workspace}/metabuild/bin/packages.rb --tar=#{File.join(artifacts,"package.tar")} tar")
end

b.target("clean") do
    b.logtitle = "Report for odp clean."

    cd odp_path
    b.run(:cmd => "make clean", :env => $env)
end

b.target("dkms") do

  b.logtitle = "Report for mppapcie_odp driver dkms packaging"

  cd odp_path

  src_tar_name = "mppapcie_odp-src"
  mkdir_p src_tar_name

  b.run(:cmd => "cd mppapcie_odp && make clean")

  cd odp_path
  b.run("cp -rfL mppapcie_odp/* #{src_tar_name}/")
  b.run("mkdir -p #{src_tar_name}/package/lib/firmware")
  b.run("cd #{src_tar_name} && tar zcf ../#{src_tar_name}.tgz ./*")
  src_tar_package = File.expand_path("#{src_tar_name}.tgz")
  cd odp_path
  b.run("rm -rf #{src_tar_name}")

  mppa_pcie_ver=`rpm -qp --qf '%{VERSION}-%{RELEASE}' $K1_TOOLCHAIN_DIR/../../../k1-mppapcie-dkms-*.rpm`.chomp()
  if mppa_pcie_ver.to_s() == "" then
      mppa_pcie_ver = `dpkg-deb -f $K1_TOOLCHAIN_DIR/../../../k1-mppapcie-dkms-*.deb  Version`.chomp()
  end
  if mppa_pcie_ver.to_s() == "" then
      raise("Could not extract mppapcie pacage version")
  end

  depends = []
  depends.push b.depends_info_struct.new("k1-mppapcie-dkms","=", mppa_pcie_ver)

  unload_script =        "# Unload the mppapcie_odp module if it is loaded\n" +
    "MPPAPCIE_ODP_IS_LOADED=$(/bin/grep -c \"^mppapcie_odp\" /proc/modules)\n" +
    "if [ \"${MPPAPCIE_ODP_IS_LOADED}\" -gt 0 ]\n" +
    "then\n" +
    "	echo \"mppapcie_odp module is loaded, unloading it\" \n" +
    "	sudo /sbin/rmmod \"mppapcie_odp\" \n" +
    "	if [ $? -ne 0 ]\n" +
    "	then\n" +
    "		echo \"[FAIL]\"\n" +
    "		exit 1\n" +
    "	fi\n" +
    "	echo \"[OK]\"\n" +
    "fi\n"

  load_script =    ""

  # Versioning is performed now using tag of the form: release-1.0.0
  (version,buildID,sha1) = repo.describe()

  # Version of tools is passed from options["version"]
  package_description = "MPPA Eth package (version:#{version} releaseID=#{buildID} sha1:#{sha1})\n"
  package_description += "This package contains Kalray's mppa ethernet driver module."

  release_info = b.release_info(version,buildID)

  pack_name = "k1-mppapcie-odp-dkms"

  pinfo = b.package_info(pack_name, release_info,
                         package_description, depends,
                         "/kernel/../extra")

  pinfo.preinst_script = unload_script
  pinfo.postinst_script = load_script
  pinfo.preun_script = unload_script
  pinfo.postun_script = ""
  pinfo.installed_files = "%attr(0755,root,root) /lib/firmware/\n%attr(0755,root,root)"

  b.create_dkms_package(src_tar_package,pinfo,["mppapcie_odp"],)
end

b.target("report-perf") do
    raise "artifacts option not set" if (options["artifacts"].empty?)
    artifacts = File.expand_path(options["artifacts"])

    cd ".metabuild"
    if File.exists?(odp_perf_files_path) then
        cd odp_perf_files_path
        b.run("tar -cvf perffiles.tar *.perf")
        b.run("mv perffiles.tar #{artifacts}")
    end
end


b.launch
