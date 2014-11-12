// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2014.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Mathias Walzer $
// $Authors: Dilek Dere, Mathias Walzer, Petra Gutenbrunner $
// --------------------------------------------------------------------------

#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/FORMAT/MzDataFile.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/CHEMISTRY/ModificationsDB.h>
#include <OpenMS/KERNEL/StandardTypes.h>
#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/SYSTEM/File.h>
#include <OpenMS/DATASTRUCTURES/String.h>
#include <OpenMS/CHEMISTRY/ModificationDefinitionsSet.h>
#include <OpenMS/FORMAT/CsvFile.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/MascotXMLFile.h>
#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/METADATA/ProteinIdentification.h>

#include <QtCore/QFile>
#include <QtCore/QProcess>
#include <QDir>

#include <fstream>
#include <map>
#include <cstddef>

//-------------------------------------------------------------
//Doxygen docu
//-------------------------------------------------------------

/**
   @page TOPP_MSGFPlusAdapter MSGFPlusAdapter

   @brief 
<CENTER>
    <table>
        <tr>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. predecessor tools </td>
            <td VALIGN="middle" ROWSPAN=2> \f$ \longrightarrow \f$ MSGFplusAdapter \f$ \longrightarrow \f$</td>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. successor tools </td>
        </tr>
        <tr>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> any signal-/preprocessing tool @n (in mzML format)</td>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> @ref TOPP_IDFilter or @n any protein/peptide processing tool</td>
        </tr>
    </table>
</CENTER>

    @em MS-GF+ must be installed before this wrapper can be used. Please make sure that Java and MS-GF+ are working.

    This adapter supports relative database filenames, which (when not found in the 
    current working directory) are looked up in the directories specified by 
    'OpenMS.ini:id_db_dir' (see @subpage TOPP_advanced).
		
    The adapter works in three steps: First MS-GF+ is run on the input MS data and the sequence database, producing an mzIdentML (.mzid) output file containing the search results. This file is then converted to a text file (.tsv) using MS-GF+' "MzIDToTsv" tool. Finally, the .tsv file is parsed and a result in idXML format is generated.

    <B>The command line parameters of this tool are:</B>
    @verbinclude TOPP_MSGFplusAdapter.cli
    <B>INI file documentation of this tool:</B>
    @htmlinclude TOPP_MSGFplusAdapter.html
*/

// We do not want this class to show up in the docu:
/// @cond TOPPCLASSES

using namespace OpenMS;
using namespace std;

class MSGFPlusAdapter :
  public TOPPBase
{
public:
  MSGFPlusAdapter() :
    TOPPBase("MSGFPlusAdapter", "MS/MS database search using MS-GF+.", false)
  {
  }

protected:
  void registerOptionsAndFlags_()
  {
    registerInputFile_("in", "<file>", "", "Input file");
    setValidFormats_("in", ListUtils::create<String>("mzML,mzXML,mgf,ms2"));
    registerOutputFile_("out", "<file>", "", "Output file");
    setValidFormats_("out", ListUtils::create<String>("idXML"));
    registerOutputFile_("mzid_out", "<file>", "", "Alternative output file", false);
    setValidFormats_("mzid_out", ListUtils::create<String>("mzid"));

    registerInputFile_("database", "<file>", "", "Protein sequence database (FASTA file). Non-existing relative filenames are looked up via 'OpenMS.ini:id_db_dir'", true, false, ListUtils::create<String>("skipexists"));
    setValidFormats_("database", ListUtils::create<String>("FASTA"));

    registerInputFile_("msgfplus_executable", "<executable>", "", "MS-GF+ .jar file, e.g. 'c:\\program files\\MSGFPlus.jar'");

    registerDoubleOption_("precursor_mass_tolerance", "<tolerance>", 20, "Precursor monoisotopic mass tolerance.", false);
    registerStringOption_("precursor_error_units", "<unit>", "ppm", "Unit to be used for precursor mass tolerance.", false);
    setValidStrings_("precursor_error_units", ListUtils::create<String>("Da,ppm"));

    registerStringOption_("isotope_error_range", "<range>", "0,1", "Range of allowed isotope peak errors. Takes into account the error introduced by choosing a non-monoisotopic peak for fragmentation. Combined with 'precursor_mass_tolerance'/'precursor_error_units', this determines the actual precursor mass tolerance. E.g. for experimental mass 'exp' and calculated mass 'calc', '-precursor_mass_tolerance 20 -precursor_error_units ppm -isotope_error_range -1,2' tests '|exp - calc - n * 1.00335 Da| < 20 ppm' for n = -1, 0, 1, 2.", false);

    registerIntOption_("decoy", "<0/1>", 0, "0: don't search decoy database, 1: search decoy database", false);
    setMinInt_("decoy", 0);
    setMaxInt_("decoy", 1);

    registerIntOption_("fragment_method", "<method>", 0, "0: as written in the spectrum (or CID if no info), 1: CID, 2: ETD, 3: HCD", false);
    setMinInt_("fragment_method", 0);
    setMaxInt_("fragment_method", 3);

    registerIntOption_("instrument", "<instrument>", 0, "0: low-res LCQ/LTQ, 1: high-res LTQ, 2: TOF, 3: Q Exactive", false);
    setMinInt_("instrument", 0);
    setMaxInt_("instrument", 3);

    registerIntOption_("enzyme", "<enzyme>", 1, "0: unspecific cleavage, 1: trypsin, 2: chymotrypsin, 3: Lys-C, 4: Lys-N, 5: glutamyl endopeptidase, 6: Arg-C, 7: Asp-N, 8: alphaLP, 9: no cleavage", false);
    setMinInt_("enzyme", 0);
    setMaxInt_("enzyme", 9);

    registerIntOption_("protocol", "<protocol>", 0, "0: No protocol, 1: phosphorylation, 2: iTRAQ, 3: iTRAQPhospho, 4: TMT", false);
    setMinInt_("protocol", 0);
    setMaxInt_("protocol", 4);

    registerIntOption_("tolerable_termini", "<num>", 2, "For trypsin, 0: non-tryptic, 1: semi-tryptic, 2: fully-tryptic peptides only", false);
    setMinInt_("tolerable_termini", 0);
    setMaxInt_("tolerable_termini", 2);
    
    registerInputFile_("mod", "<file>", "", "Modification configuration file", false);

    registerIntOption_("min_precursor_charge", "<charge>", 2, "Minimum precursor ion charge", false);
    registerIntOption_("max_precursor_charge", "<charge>", 3, "Maximum precursor ion charge", false);

    registerIntOption_("min_peptide_length", "<length>", 6, "Minimum peptide length to consider", false);
    registerIntOption_("max_peptide_length", "<length>", 40, "Maximum peptide length to consider", false);   

    registerIntOption_("matches_per_spec", "<num>", 1, "Number of matches per spectrum to be reported", false);
    registerIntOption_("add_features", "<num>", 0, "0: output basic scores only, 1: output additional features", false);
    setMinInt_("add_features", 0);
    setMaxInt_("add_features", 1);

    registerIntOption_("java_memory", "<num>", 3500, "Maximum Java heap size (in MB)", false);
    registerIntOption_("java_permgen", "<num>", 0, "Maximum Java permanent generation space (in MB); only for Java 7 and below", false);
  }

  // The following sequence modification methods are used to modify the sequence stored in the TSV such that it can be used by AASequence

  // Method to cut the first and last character of the sequence.
  // The sequences in the tsv file has the form K.AAAA.R (AAAA stands for any amino acid sequence.
  // After this method is used the sequence AAAA results
  String cutSequence (String sequence)
  {
    String modifiedSequence = sequence;

    //search for the first and last occurence of .
    std::size_t findFirst = sequence.find_first_of(".");
    std::size_t findLast = sequence.find_last_of(".");
       
    //used the found positions and cut the sequence 
    if (findFirst!=std::string::npos && findLast!=std::string::npos && findFirst != findLast)
    {
      modifiedSequence = sequence.substr(findFirst+1, findLast-2);
    }
		
    return modifiedSequence;
  }

  // Method to replace comma by point.
  // This is used as point should be used as separator of decimals instead of comma
  String fixDecimalSeparator (String seq)
  {
    std::size_t found = seq.find_first_of(".,");
    while (found!=std::string::npos) 
    {
      seq[found]='.';
      found=seq.find_first_of(".,",found+1);
    }
    return seq;
  }

  String modifyNTermAASpecificSequence (String seq) {
    String swap = "";
    string modifiedSequence(seq);
    vector<pair<String, char> > massShiftList;

    massShiftList.push_back(make_pair("-18.011", 'E'));
    massShiftList.push_back(make_pair("-17.027", 'Q'));

    for (vector<pair<String, char> >::const_iterator iter = massShiftList.begin(); iter != massShiftList.end(); iter++)
    {
      string modMassShift = iter->first;
      size_t found = modifiedSequence.find(modMassShift);

      if (found != string::npos)
      {
        String tmp = modifiedSequence.substr(0, found + modMassShift.length() + 1);
        size_t foundAA  = tmp.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ");

        if ((foundAA > found) && (tmp[foundAA] == iter->second)) // no AA at the begin
        {
          if (found > 0)
          {
            swap = modifiedSequence.substr(0, found);
          }          
          return swap += *tmp.rbegin() + modMassShift + modifiedSequence.substr(found + modMassShift.length() + 1);
        }
      }
    }
    return  modifiedSequence;
  }
	
  // Method to replace the mass representation of modifications.
  // Modifications in the tsv file has the form M+15.999 e.g.
  // After using this method the sequence should look like this: M[+15.999] 
  String modifySequence (String seq)
  {
    String modifiedSequence = seq;
	std::size_t found = modifiedSequence.find_first_of("+-");
    while (found!=std::string::npos)
    {
      modifiedSequence = modifiedSequence.insert(found, "[");
      std::size_t found1 = modifiedSequence.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ", found);
      if (found1!=std::string::npos)
      {
        modifiedSequence.insert(found1, "]");
        found = modifiedSequence.find_first_of("+-", found1+2);
      } 
      else 
      { //if last amino acid is modified
        modifiedSequence = modifiedSequence + "]";
        return modifiedSequence;
      }
    }
    return modifiedSequence;
  }

  // Parse mzML and create RTMapping
  // get RT: it doesn't exist in output from MS-GF+
  // get m/z: it is rounded after converting to TSV
  void generateInputfileMapping(Map<String, vector<float> >& rt_mapping)
  {
    String exp_name = getStringOption_("in");

    if (!exp_name.empty())
    {
      PeakMap exp;
      // load only MS2 spectra:
      MzMLFile f;
      f.getOptions().addMSLevel(2);
      f.load(exp_name, exp);

      for (PeakMap::iterator it = exp.begin(); it != exp.end(); ++it)
      {
        String id = it->getNativeID(); // expected format: "... scan=#"
        if (id != "") 
        {
          rt_mapping[id].push_back(it->getRT());
          rt_mapping[id].push_back(it->getPrecursors()[0].getMZ());
        }
      }     
    }
  }  

  ExitCodes main_(int, const char**)
  {
    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    String inputfile_name = getStringOption_("in");
    writeDebug_(String("Input file: ") + inputfile_name, 1);
    if (inputfile_name == "")
    {
      writeLog_("No input file specified. Aborting!");
      printUsage_();
      return ILLEGAL_PARAMETERS;
    }

    String outputfile_name = getStringOption_("out");
    writeDebug_(String("Output file: ") + outputfile_name, 1);
    if (outputfile_name == "")
    {
      writeLog_("No output file specified. Aborting!");
      printUsage_();
      return ILLEGAL_PARAMETERS;
    }

    String db_name(getStringOption_("database"));
    if (!File::readable(db_name))
    {
      String full_db_name;
      try
      {
        full_db_name = File::findDatabase(db_name);
      }
      catch (...)
      {
        printUsage_();
        return ILLEGAL_PARAMETERS;
      }
      db_name = full_db_name;
    }

    // write the MS-GF+ output file to the temporary directory
    String temp_directory = QDir::toNativeSeparators((File::getTempDirectory() + "/" + File::getUniqueName() + "/").toQString());
    {
      QDir d;
      d.mkpath(temp_directory.toQString());
    }

    String msgfplus_output_filename_ori = getStringOption_("mzid_out");
    String msgfplus_output_filename = msgfplus_output_filename_ori;
    bool remove_output_suffix = false;

    if (msgfplus_output_filename == "")
    {
      msgfplus_output_filename = temp_directory + "msgfplus_output_file.mzid";
    } 
    else if (msgfplus_output_filename.suffix('.') != "mzid") 
    {
      msgfplus_output_filename += ".mzid";
      remove_output_suffix = true;
    }

    QString java_memory = "-Xmx" + QString(getIntOption_("java_memory")) + "m";
    QString msgfplus_exe = getStringOption_("msgfplus_executable").toQString();
    QString precursor_tol = QString::number(getDoubleOption_("precursor_mass_tolerance")) + getStringOption_("precursor_error_units").toQString();
    QStringList process_params; // the actual process is Java, not MS-GF+!
    process_params << java_memory
                   << "-jar" << msgfplus_exe
                   << "-s" << inputfile_name.toQString()
                   << "-o" << msgfplus_output_filename.toQString()
                   << "-d" << db_name.toQString()
                   << "-t" << precursor_tol
                   << "-ti" << getStringOption_("isotope_error_range").toQString()
                   << "-tda" << QString(getIntOption_("decoy"))
                   << "-m" << QString(getIntOption_("fragment_method"))
                   << "-inst" << QString(getIntOption_("instrument"))
                   << "-e" << QString(getIntOption_("enzyme"))
                   << "-protocol" << QString(getIntOption_("protocol"))
                   << "-ntt" << QString(getIntOption_("tolerable_termini"))
                   << "-minLength" << QString(getIntOption_("min_peptide_length"))
                   << "-maxLength" << QString(getIntOption_("max_peptide_length"))
                   << "-minCharge" << QString(getIntOption_("min_precursor_charge"))
                   << "-maxCharge" << QString(getIntOption_("max_precursor_charge"))
                   << "-n" << QString(getIntOption_("matches_per_spec"))
                   << "-addFeatures" << QString(getIntOption_("add_features"))
                   << "-thread" << QString(getIntOption_("threads"));

    // TODO: create mod database on the fly from fixed and variable mod params
    String modfile_name = getStringOption_("mod");
    if (!modfile_name.empty())
    {
      process_params << "-mod" << getStringOption_("mod").toQString();
    }

    //-------------------------------------------------------------
    // execute MS-GF+
    //-------------------------------------------------------------
   
    // run MS-GF+ process and create the .mzid file
    int status = QProcess::execute("java", process_params);
    if (status != 0)
    {
      writeLog_("Fatal error: Running MS-GF+ returned an error code. Does the MS-GF+ executable (.jar file) exist?");
      return EXTERNAL_PROGRAM_ERROR;
    }

    //-------------------------------------------------------------
    // execute TSV converter
    //------------------------------------------------------------- 

    String mzidtotsv_output_filename = temp_directory + "svFile.tsv";
    int java_permgen = getIntOption_("java_permgen");
    process_params.clear();
    process_params << java_memory;
    if (java_permgen > 0) 
    {
      process_params << "-XX:MaxPermSize=" + QString(java_permgen) + "m";
    }
    process_params << "-cp" << msgfplus_exe << "edu.ucsd.msjava.ui.MzIDToTsv"
                   << "-i" << msgfplus_output_filename.toQString()
                   << "-o" << mzidtotsv_output_filename.toQString()
                   << "-showQValue" << "1"
                   << "-showDecoy" << "1"
                   << "-unroll" << "1";
    status = QProcess::execute("java", process_params);
    if (status != 0)
    {
      writeLog_("Fatal error: Running MzIDToTSVConverter returned an error code.");
      return EXTERNAL_PROGRAM_ERROR;
    }

    //-------------------------------------------------------------
    // create idXML
    //------------------------------------------------------------- 

    // initialize map
    Map<String, vector<float> > rt_mapping;
    generateInputfileMapping(rt_mapping);

    CsvFile tsvfile(mzidtotsv_output_filename, '\t');

    // handle the search parameters
    ProteinIdentification::DigestionEnzyme enzyme_type;
    Int enzyme_code = getIntOption_("enzyme");

    if (enzyme_code == 0) 
    {
      enzyme_type = ProteinIdentification::UNKNOWN_ENZYME;
    }
    else if (enzyme_code == 1) 
    {
      enzyme_type = ProteinIdentification::TRYPSIN;
    } 
    else if (enzyme_code == 2) 
    {
      enzyme_type = ProteinIdentification::CHYMOTRYPSIN;
    }
    else if (enzyme_code == 9) 
    {
      enzyme_type = ProteinIdentification::NO_ENZYME ;     
    }
    else enzyme_type = ProteinIdentification::UNKNOWN_ENZYME;

    ProteinIdentification::SearchParameters search_parameters;
    search_parameters.db = getStringOption_("database");
    search_parameters.charges = "+" + String(getIntOption_("min_precursor_charge")) + "-+" + String(getIntOption_("max_precursor_charge"));

    ProteinIdentification::PeakMassType mass_type = ProteinIdentification::MONOISOTOPIC;
    search_parameters.mass_type = mass_type;
    //search_parameters.fixed_modifications = getStringList_("fixed_modifications"); // TODO: Parse mod config file
    //search_parameters.variable_modifications = getStringList_("variable_modifications"); // TODO: Parse mod config file
    search_parameters.precursor_tolerance = getDoubleOption_("precursor_mass_tolerance"); // TODO: convert values to Dalton if not already Dalton
    search_parameters.enzyme = enzyme_type;

    // create idXML file
    vector<ProteinIdentification> protein_ids;
    ProteinIdentification protein_id;

    DateTime now = DateTime::now();
    String date_string = now.getDate();
    String identifier("MS-GF+_" + date_string);

    protein_id.setIdentifier(identifier);
    protein_id.setDateTime(now);
    protein_id.setSearchParameters(search_parameters);
    protein_id.setSearchEngineVersion("");
    protein_id.setSearchEngine("MS-GF+");
    protein_id.setScoreType("MS-GF+");

    // store all peptide identifications in a map, the key is the scan number
    map<int, PeptideIdentification> peptide_identifications;
    set<String> prot_accessions;

    double score; // use SpecEValue from the TSV file
    UInt rank; 
    Int charge;
    AASequence sequence;
    int scanNumber;

    // iterate over the rows of the TSV file
    for (Size row_count = 1; row_count < tsvfile.rowCount(); ++row_count)
    {
      vector<String> elements;
      if (!tsvfile.getRow(row_count, elements))
      {
        writeLog_("Error: could not split row " + String(row_count) + " of file '" + mzidtotsv_output_filename + "'");
        return PARSE_ERROR;
      }

      if ((elements[2] == "") || (elements[2] == "-1")) 
      {
        scanNumber = elements[1].suffix('=').toInt();
      } 
      else 
      {
        scanNumber = elements[2].toInt();
      }
      
      sequence = AASequence::fromString(modifySequence(modifyNTermAASpecificSequence(fixDecimalSeparator(cutSequence(elements[8])))));
      vector<PeptideHit> p_hits;
      String prot_accession = elements[9];

      if (prot_accessions.find(prot_accession) == prot_accessions.end()) 
      {
        prot_accessions.insert(prot_accession);
      }

      if (peptide_identifications.find(scanNumber) == peptide_identifications.end()) 
      {
        score = elements[12].toDouble();
        rank = 0; // set to 0 at the moment
        charge = elements[7].toInt();
        
        PeptideHit p_hit(score, rank, charge, sequence);
        p_hit.addProteinAccession(prot_accession);
        p_hits.push_back(p_hit);

        String spec_id = elements[1];
        peptide_identifications[scanNumber].setRT(rt_mapping[spec_id][0]);
        peptide_identifications[scanNumber].setMZ(rt_mapping[spec_id][1]);

        peptide_identifications[scanNumber].setMetaValue("ScanNumber", scanNumber);        
        peptide_identifications[scanNumber].setScoreType("SpecEValue");
        peptide_identifications[scanNumber].setHigherScoreBetter(false);
        peptide_identifications[scanNumber].setIdentifier(identifier);
      } 
      else 
      {
        p_hits = peptide_identifications[scanNumber].getHits();
        for (vector<PeptideHit>::iterator p_it = p_hits.begin(); p_it != p_hits.end(); ++ p_it)
        {
          if (p_it -> getSequence() == sequence) 
          {
            p_it -> addProteinAccession(prot_accession);
          }
        }
      }
      peptide_identifications[scanNumber].setHits(p_hits);
    }

    vector<ProteinHit> prot_hits;
    for (set<String>::iterator it = prot_accessions.begin(); it != prot_accessions.end(); ++ it) 
    {
      ProteinHit prot_hit = ProteinHit();
      prot_hit.setAccession(*it);
      prot_hits.push_back(prot_hit);
    }
    protein_id.setHits(prot_hits);
    protein_ids.push_back(protein_id);

    // iterate over map and create a vector of peptide identifications
    map<int, PeptideIdentification>::iterator it;
    vector<PeptideIdentification> peptide_ids;
    PeptideIdentification pep;
    for (map<int, PeptideIdentification>::iterator it = peptide_identifications.begin(); 
        it != peptide_identifications.end(); ++ it)
    {
      pep = it->second;
      pep.sort();
      peptide_ids.push_back(pep);
    }
    
    IdXMLFile().store(outputfile_name, protein_ids, peptide_ids);

    if (remove_output_suffix) 
    {
      QFile::rename(msgfplus_output_filename.toQString(), msgfplus_output_filename_ori.toQString());
    }

    return EXECUTION_OK;
  }	
};


int main(int argc, const char** argv)
{
  MSGFPlusAdapter tool;

  return tool.main(argc, argv);
}
