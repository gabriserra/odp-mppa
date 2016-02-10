#!/usr/bin/ruby
# coding: utf-8

$LOAD_PATH.push('metabuild/lib')
require 'metabuild'
include Metabuild
CONFIGS = `make list-configs`.split(" ").inject({}){|x, c| x.merge({ c => {} })}

options = Options.new({ "k1tools"       => [ENV["K1_TOOLCHAIN_DIR"].to_s,"Path to a valid compiler prefix."],
                        "debug"         => {"type" => "boolean", "default" => false, "help" => "Debug mode." },
                        "list-configs"  => {"type" => "boolean", "default" => false, "help" => "List all targets" },
                        "local-valid"   => {"type" => "boolean", "default" => false, "help" => "Valid using the local installation" },
                        "configs"       => {"type" => "string", "default" => CONFIGS.keys.join(" "), "help" => "Build configs. Default = #{CONFIGS.keys.join(" ")}" },
                        "valid-configs" => {"type" => "string", "default" => CONFIGS.keys.join(" "), "help" => "Build configs. Default = #{CONFIGS.keys.join(" ")}" },
                        "output-dir"    => [nil, "Output directory for RPMs."],
                        "k1version"     => ["unknown", "K1 Tools version required for building ODP applications"],
                        "toolchainversion"     => ["", "K1 Toolchain version required for building ODP applications"],
                     })

if options["list-configs"] == true then
    puts CONFIGS.map(){|n, i| n}.join("\n")
    exit 0
end
workspace  = options["workspace"]
odp_clone  = options['clone']
jobs = options['jobs']

odp_path   = File.join(workspace,odp_clone)

k1tools = options["k1tools"]

env = {}
env["K1_TOOLCHAIN_DIR"] = k1tools
env["PATH"] = "#{k1tools}/bin:#{ENV["PATH"]}"
env["LD_LIBRARY_PATH"] = "#{k1tools}/lib:#{k1tools}/lib64:#{ENV["LD_LIBRARY_PATH"]}"

repo = Git.new(odp_clone,workspace)

local_valid = options["local-valid"]

clean = Target.new("clean", repo, [])
build = ParallelTarget.new("build", repo, [])
valid = ParallelTarget.new("valid", repo, [build])

long = nil
apps = nil

install = Target.new("install", repo, [build])
if local_valid then
        long = Target.new("long", repo, [install])
        apps = Target.new("apps", repo, [install])
else
        long = Target.new("long", repo, [])
        apps = Target.new("apps", repo, [])
end
dkms = Target.new("dkms", repo, [])
package = Target.new("package", repo, [install, apps])

b = Builder.new("odp", options, [clean, build, valid, long, apps, dkms, package, install])

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

    when "fedora19-64","debian6-64","debian7-64", /MPPADevelopers*/, /MPPAEthDevelopers*/
        # Validate nothing.
        valid_configs = [ ]
    when "fedora17-64"
        configs= [ "k1b-kalray-nodeos_explorer", "k1b-kalray-mos_explorer" ]
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

b.target("build") do
    b.logtitle = "Report for odp build."
    cd odp_path
    b.run(:cmd => "make build CONFIGS='#{configs.join(" ")}'")
end

b.target("valid") do
    b.logtitle = "Report for odp tests."
    cd odp_path

    b.valid(:cmd => "make valid CONFIGS='#{valid_configs.join(" ")}'")

    if options['logtype'] == :junit then
        fName=File.dirname(options['logfile']) + "/" + "automake-tests.xml"
        b.valid(:cmd => "make junits CONFIGS='#{valid_configs.join(" ")}' JUNIT_FILE=#{fName}")
    end
end


b.target("long") do
    b.logtitle = "Report for odp tests."
    cd odp_path

    make_opt = ""
    if not local_valid then
        make_opt = "USE_PACKAGES=1"
    end

    b.run(:cmd => "make long #{make_opt} CONFIGS='#{valid_configs.join(" ")}'")

    valid_configs.each(){|conf|
        cd File.join(odp_path, "build", "long", conf, "bin")
        b.ctest( {
                     :ctest_args => "-L #{valid_type}",
                     :fail_msg => "Failed to validate #{conf}",
                     :success_msg => "Successfully validated #{conf}"
                 })
    }
end

b.target("apps") do
    b.logtitle = "Report for odp apps."
    cd odp_path

    make_opt = ""
    if not local_valid then
        make_opt = "USE_PACKAGES=1"
    end

    b.run(:cmd => "make apps-install #{make_opt}")

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

    b.run(:cmd => "cd install/; tar cf ../odp.tar local/k1tools/lib/ local/k1tools/share/odp/firmware local/k1tools/share/odp/build/ local/k1tools/share/odp/skel/ local/k1tools/k1*/include local/k1tools/doc/ local/k1tools/lib64", :env => env)
    b.run(:cmd => "cd install/; tar cf ../odp-tests.tar local/k1tools/share/board local/k1tools/share/odp/*/examples", :env => env)
    b.run(:cmd => "cd install/; tar cf ../odp-apps-internal.tar local/k1tools/share/odp/apps", :env => env)
    b.run(:cmd => "cd install/; tar cf ../odp-cunit.tar local/k1tools/kalray_internal/cunit", :env => env)

    (version,releaseID,sha1) = repo.describe()
    release_info = b.release_info(version,releaseID,sha1)
    sha1 = repo.sha1()

    #K1 ODP
    tar_package = File.expand_path("odp.tar")
    depends = []
    depends.push b.depends_info_struct.new("k1-tools","=", options["k1version"], "")
    if options["toolchainversion"].to_s != "" then
        depends.push b.depends_info_struct.new("k1-toolchain","=", options["toolchainversion"], "")
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
    output_parameters = File.join(artifacts,"k1odp_parameters.sh")
    b.run("rm -f #{output_parameters}")
    b.run("echo 'K1ODP_VERSION=#{$version}-#{$buildID}' >> #{output_parameters}")
    b.run("echo 'K1ODP_RELEASE=#{$version}' >> #{output_parameters}")
    b.run("echo 'COMMITER_EMAIL=#{options["email"]}' >> #{output_parameters}")
    b.run("echo 'INTEGRATION_BRANCH=#{ENV.fetch("INTEGRATION_BRANCH",options["branch"])}' >> #{output_parameters}")
    b.run("echo 'REVISION=#{repo.long_sha1()}' >> #{output_parameters}")
    b.run("#{workspace}/metabuild/bin/packages.rb --tar=#{File.join(artifacts,"package.tar")} tar")

end

b.target("clean") do
    b.logtitle = "Report for odp clean."

    cd odp_path
    b.run(:cmd => "make clean", :env => env)
end

b.target("dkms") do

  b.logtitle = "Report for mppaeth driver dkms packaging"

  cd odp_path

  src_tar_name = "mppaeth-src"
  mkdir_p src_tar_name

  b.run(:cmd => "cd mppaeth && make clean")

  cd odp_path
  b.run("cp -rfL mppaeth/* #{src_tar_name}/")
	b.run("mkdir -p #{src_tar_name}/package/lib/firmware/kalray")
  b.run("cd #{src_tar_name} && tar zcf ../#{src_tar_name}.tgz ./*")
  src_tar_package = File.expand_path("#{src_tar_name}.tgz")
  cd odp_path
  b.run("rm -rf #{src_tar_name}")

  mppa_pcie_ver=`rpm -qp --qf '%{VERSION}-%{RELEASE}' $K1_TOOLCHAIN_DIR/../../../k1-mppapcie-dkms-*.rpm`
  depends = []
  depends.push b.depends_info_struct.new("k1-mppapcie-dkms","=", mppa_pcie_ver)

  unload_script =        "# Unload the mppaeth module if it is loaded\n" +
    "MPPAETH_IS_LOADED=$(/bin/grep -c \"^mppaeth\" /proc/modules)\n" +
    "if [ \"${MPPAETH_IS_LOADED}\" -gt 0 ]\n" +
    "then\n" +
    "	echo \"mppaeth module is loaded, unloading it\" \n" +
    "	sudo /sbin/rmmod \"mppaeth\" \n" +
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

  pack_name = "k1-mppaeth-dkms"

  pinfo = b.package_info(pack_name, release_info,
                         package_description, depends,
                         "/kernel/../extra")

  pinfo.preinst_script = unload_script
  pinfo.postinst_script = load_script
  pinfo.preun_script = unload_script
  pinfo.postun_script = ""
  pinfo.installed_files = "%attr(0755,root,root) /lib/firmware/\n%attr(0755,root,root)"

  b.create_dkms_package(src_tar_package,pinfo,["mppaeth"],)

  # Test installation of the driver from the package
  if $execution_platform == "hw" then
    cd workspace
    package = b.find_package(pack_name,"#{release_info.version}-#{release_info.full_buildID}");
    package_version = b.get_package_version(package[pack_name]);
    b.run(:cmd => "echo \"mppaeth package file #{package[pack_name]} version: #{package_version} full_version #{release_info.full_version}\"", :verbose => true);
    b.run(:cmd => "#{workspace}/metabuild/bin/packages.rb --nodeps install --list #{pack_name} --vers #{package_version}", :verbose => true);
    b.run(:cmd => "sudo rmmod mppaeth && sudo modprobe mppaeth", :env => $extra_env,:verbose => true);
  end

end


b.launch

