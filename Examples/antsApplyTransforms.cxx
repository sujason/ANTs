#include "antsUtilities.h"
#include "antsAllocImage.h"
#include "itkantsRegistrationHelper.h"
#include "ReadWriteImage.h"

#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkResampleImageFilter.h"
#include "itkVectorIndexSelectionCastImageFilter.h"

#include "itkAffineTransform.h"
#include "itkCompositeTransform.h"
#include "itkDisplacementFieldTransform.h"
#include "itkIdentityTransform.h"
#include "itkMatrixOffsetTransformBase.h"
#include "itkTransformFactory.h"
#include "itkTransformFileReader.h"
#include "itkTransformToDisplacementFieldSource.h"

#include "itkBSplineInterpolateImageFunction.h"
#include "itkLinearInterpolateImageFunction.h"
#include "itkGaussianInterpolateImageFunction.h"
#include "itkInterpolateImageFunction.h"
#include "itkNearestNeighborInterpolateImageFunction.h"
#include "itkWindowedSincInterpolateImageFunction.h"
#include "itkLabelImageGaussianInterpolateImageFunction.h"

namespace ants
{
template <typename TensorImageType, typename ImageType>
void
CorrectImageTensorDirection( TensorImageType * movingTensorImage, ImageType * referenceImage )
{
  typedef typename TensorImageType::DirectionType DirectionType;

  typename DirectionType::InternalMatrixType direction =
    movingTensorImage->GetDirection().GetTranspose() * referenceImage->GetDirection().GetVnlMatrix();

  if( !direction.is_identity( 0.00001 ) )
    {
    itk::ImageRegionIterator<TensorImageType> It( movingTensorImage, movingTensorImage->GetBufferedRegion() );
    for( It.GoToBegin(); !It.IsAtEnd(); ++It )
      {
      typename TensorImageType::PixelType tensor = It.Get();

      typename TensorImageType::DirectionType::InternalMatrixType dt;
      dt(0, 0) = tensor[0];
      dt(0, 1) = dt(1, 0) = tensor[1];
      dt(0, 2) = dt(2, 0) = tensor[2];
      dt(1, 1) = tensor[3];
      dt(1, 2) = dt(2, 1) = tensor[4];
      dt(2, 2) = tensor[5];

      dt = direction * dt * direction.transpose();

      tensor[0] = dt(0, 0);
      tensor[1] = dt(0, 1);
      tensor[2] = dt(0, 2);
      tensor[3] = dt(1, 1);
      tensor[4] = dt(1, 2);
      tensor[5] = dt(2, 2);

      It.Set( tensor );
      }
    }
}

template <typename DisplacementFieldType, typename ImageType>
void
CorrectImageVectorDirection( DisplacementFieldType * movingVectorImage, ImageType * referenceImage )
{
  typedef typename DisplacementFieldType::DirectionType DirectionType;

  typename DirectionType::InternalMatrixType direction =
    movingVectorImage->GetDirection().GetTranspose() * referenceImage->GetDirection().GetVnlMatrix();

  typedef typename DisplacementFieldType::PixelType VectorType;
  typedef typename VectorType::ComponentType        ComponentType;

  const unsigned int dimension = ImageType::ImageDimension;

  if( !direction.is_identity( 0.00001 ) )
    {
    itk::ImageRegionIterator<DisplacementFieldType> It( movingVectorImage, movingVectorImage->GetBufferedRegion() );
    for( It.GoToBegin(); !It.IsAtEnd(); ++It )
      {
      VectorType vector = It.Get();

      vnl_vector<ComponentType> internalVector( dimension );
      for( unsigned int d = 0; d < dimension; d++ )
        {
        internalVector[d] = vector[d];
        }

      internalVector.pre_multiply( direction );;
      for( unsigned int d = 0; d < dimension; d++ )
        {
        vector[d] = internalVector[d];
        }

      It.Set( vector );
      }
    }
}

template <unsigned int Dimension>
int antsApplyTransforms( itk::ants::CommandLineParser::Pointer & parser, unsigned int inputImageType = 0 )
{
  typedef double                           RealType;
  typedef double                           PixelType;
  typedef itk::Vector<RealType, Dimension> VectorType;

  typedef itk::Image<PixelType, Dimension>  ImageType;
  typedef itk::Image<VectorType, Dimension> DisplacementFieldType;
  typedef itk::Image<char, Dimension>       ReferenceImageType;

  typedef itk::SymmetricSecondRankTensor<RealType, Dimension> TensorPixelType;
  typedef itk::Image<TensorPixelType, Dimension>              TensorImageType;

  const unsigned int NumberOfTensorElements = 6;

  typename TensorImageType::Pointer tensorImage = NULL;
  typename DisplacementFieldType::Pointer vectorImage = NULL;

  std::vector<typename ImageType::Pointer> inputImages;
  inputImages.clear();

  std::vector<typename ImageType::Pointer> outputImages;
  outputImages.clear();

  /**
   * Input object option - for now, we're limiting this to images.
   */
  typename itk::ants::CommandLineParser::OptionType::Pointer inputOption = parser->GetOption( "input" );
  typename itk::ants::CommandLineParser::OptionType::Pointer outputOption = parser->GetOption( "output" );

  if( inputImageType == 2 && inputOption && inputOption->GetNumberOfValues() > 0 )
    {
    antscout << "Input tensor image: " << inputOption->GetValue() << std::endl;

    ReadTensorImage<TensorImageType>( tensorImage, ( inputOption->GetValue() ).c_str(), true );
    }
  else if( inputImageType == 0 && inputOption && inputOption->GetNumberOfValues() > 0 )
    {
    antscout << "Input scalar image: " << inputOption->GetValue() << std::endl;

    typedef itk::ImageFileReader<ImageType> ReaderType;
    typename ReaderType::Pointer reader = ReaderType::New();
    reader->SetFileName( ( inputOption->GetValue() ).c_str() );
    reader->Update();

    inputImages.push_back( reader->GetOutput() );
    }
  else if( inputImageType == 1 && inputOption && inputOption->GetNumberOfValues() > 0 )
    {
    antscout << "Input vector image: " << inputOption->GetValue() << std::endl;

    typedef itk::ImageFileReader<DisplacementFieldType> ReaderType;
    typename ReaderType::Pointer reader = ReaderType::New();
    reader->SetFileName( ( inputOption->GetValue() ).c_str() );

    try
      {
      vectorImage = reader->GetOutput();
      vectorImage->Update();
      vectorImage->DisconnectPipeline();
      }
    catch( ... )
      {
      std::cerr << "Unable to read vector image " << reader->GetFileName() << std::endl;
      return EXIT_FAILURE;
      }
    }
  else if( outputOption && outputOption->GetNumberOfValues() > 0 )
    {
    if( outputOption->GetNumberOfParameters( 0 ) > 1 &&
        parser->Convert<unsigned int>( outputOption->GetParameter( 0, 1 ) ) == 0 )
      {
      antscout << "An input image is required." << std::endl;
      return EXIT_FAILURE;
      }
    }

  /**
   * Reference image option
   */

  // read in the image as char since we only need the header information.
  typedef itk::Image<char, Dimension> ReferenceImageType;
  typename ReferenceImageType::Pointer referenceImage;

  typename itk::ants::CommandLineParser::OptionType::Pointer referenceOption =
    parser->GetOption( "reference-image" );
  if( referenceOption && referenceOption->GetNumberOfValues() > 0 )
    {
    antscout << "Reference image: " << referenceOption->GetValue() << std::endl;

    // read in the image as char since we only need the header information.
    typedef itk::ImageFileReader<ReferenceImageType> ReferenceReaderType;
    typename ReferenceReaderType::Pointer referenceReader =
      ReferenceReaderType::New();
    referenceReader->SetFileName( ( referenceOption->GetValue() ).c_str() );

    referenceImage = referenceReader->GetOutput();
    referenceImage->Update();
    referenceImage->DisconnectPipeline();
    }
  else
    {
    antscout << "Error:  No reference image specified." << std::endl;
    return EXIT_FAILURE;
    }

  if( inputImageType == 1 )
    {
    CorrectImageVectorDirection<DisplacementFieldType, ReferenceImageType>( vectorImage, referenceImage );
    for( unsigned int i = 0; i < Dimension; i++ )
      {
      typedef itk::VectorIndexSelectionCastImageFilter<DisplacementFieldType, ImageType> SelectorType;
      typename SelectorType::Pointer selector = SelectorType::New();
      selector->SetInput( vectorImage );
      selector->SetIndex( i );
      selector->Update();

      inputImages.push_back( selector->GetOutput() );
      }
    }
  else if( inputImageType == 2 )
    {
    CorrectImageTensorDirection<TensorImageType, ReferenceImageType>( tensorImage, referenceImage );
    for( unsigned int i = 0; i < NumberOfTensorElements; i++ )
      {
      typedef itk::VectorIndexSelectionCastImageFilter<TensorImageType, ImageType> SelectorType;
      typename SelectorType::Pointer selector = SelectorType::New();
      selector->SetInput( tensorImage );
      selector->SetIndex( i );
      selector->Update();

      inputImages.push_back( selector->GetOutput() );
      }
    }

  /**
   * Transform option
   */
  // Register the matrix offset transform base class to the
  // transform factory for compatibility with the current ANTs.
  typedef itk::MatrixOffsetTransformBase<double, Dimension, Dimension> MatrixOffsetTransformType;
  itk::TransformFactory<MatrixOffsetTransformType>::RegisterTransform();
  typedef itk::MatrixOffsetTransformBase<double, Dimension, Dimension> MatrixOffsetTransformType;
  itk::TransformFactory<MatrixOffsetTransformType>::RegisterTransform();

  typedef itk::CompositeTransform<double, Dimension> CompositeTransformType;
  typename itk::ants::CommandLineParser::OptionType::Pointer transformOption = parser->GetOption( "transform" );

  std::vector<bool> isDerivedTransform;
  typename CompositeTransformType::Pointer compositeTransform =
    GetCompositeTransformFromParserOption<Dimension>( parser, transformOption, isDerivedTransform );
  if( compositeTransform.IsNull() )
    {
    return EXIT_FAILURE;
    }

  /**
   * Interpolation option
   */
  typedef itk::LinearInterpolateImageFunction<ImageType, RealType> LinearInterpolatorType;
  typename LinearInterpolatorType::Pointer linearInterpolator = LinearInterpolatorType::New();

  typedef itk::NearestNeighborInterpolateImageFunction<ImageType, RealType> NearestNeighborInterpolatorType;
  typename NearestNeighborInterpolatorType::Pointer nearestNeighborInterpolator =
    NearestNeighborInterpolatorType::New();

  typedef itk::BSplineInterpolateImageFunction<ImageType, RealType> BSplineInterpolatorType;
  typename BSplineInterpolatorType::Pointer bSplineInterpolator = BSplineInterpolatorType::New();

  typedef itk::GaussianInterpolateImageFunction<ImageType, RealType> GaussianInterpolatorType;
  typename GaussianInterpolatorType::Pointer gaussianInterpolator = GaussianInterpolatorType::New();

  typedef itk::WindowedSincInterpolateImageFunction<ImageType, 3> HammingInterpolatorType;
  typename HammingInterpolatorType::Pointer hammingInterpolator = HammingInterpolatorType::New();

  typedef itk::WindowedSincInterpolateImageFunction<ImageType, 3,
                                                    itk::Function::CosineWindowFunction<3> > CosineInterpolatorType;
  typename CosineInterpolatorType::Pointer cosineInterpolator = CosineInterpolatorType::New();

  typedef itk::WindowedSincInterpolateImageFunction<ImageType, 3,
                                                    itk::Function::WelchWindowFunction<3> > WelchInterpolatorType;
  typename WelchInterpolatorType::Pointer welchInterpolator = WelchInterpolatorType::New();

  typedef itk::WindowedSincInterpolateImageFunction<ImageType, 3,
                                                    itk::Function::LanczosWindowFunction<3> > LanczosInterpolatorType;
  typename LanczosInterpolatorType::Pointer lanczosInterpolator = LanczosInterpolatorType::New();

  typedef itk::WindowedSincInterpolateImageFunction<ImageType, 3,
                                                    itk::Function::BlackmanWindowFunction<3> > BlackmanInterpolatorType;
  typename BlackmanInterpolatorType::Pointer blackmanInterpolator = BlackmanInterpolatorType::New();

  const unsigned int NVectorComponents = 1;
  typedef VectorPixelCompare<RealType, NVectorComponents> CompareType;
  typedef typename itk::LabelImageGaussianInterpolateImageFunction<ImageType, RealType,
                                                                   CompareType> MultiLabelInterpolatorType;
  typename MultiLabelInterpolatorType::Pointer multiLabelInterpolator = MultiLabelInterpolatorType::New();

  std::string whichInterpolator( "linear" );
  typedef itk::InterpolateImageFunction<ImageType, RealType> InterpolatorType;
  typename InterpolatorType::Pointer interpolator = NULL;

  typename itk::ants::CommandLineParser::OptionType::Pointer interpolationOption =
    parser->GetOption( "interpolation" );
  if( interpolationOption && interpolationOption->GetNumberOfValues() > 0 )
    {
    whichInterpolator = interpolationOption->GetValue();
    ConvertToLowerCase( whichInterpolator );

    if( !std::strcmp( whichInterpolator.c_str(), "nearestneighbor" ) )
      {
      interpolator = nearestNeighborInterpolator;
      }
    else if( !std::strcmp( whichInterpolator.c_str(), "bspline" ) )
      {
      if( interpolationOption->GetNumberOfParameters() > 0 )
        {
        unsigned int bsplineOrder = parser->Convert<unsigned int>( interpolationOption->GetParameter( 0, 0 ) );
        bSplineInterpolator->SetSplineOrder( bsplineOrder );
        }
      interpolator = bSplineInterpolator;
      }
    else if( !std::strcmp( whichInterpolator.c_str(), "gaussian" ) )
      {
      double sigma[Dimension];
      for( unsigned int d = 0; d < Dimension; d++ )
        {
        sigma[d] = inputImages[0]->GetSpacing()[d];
        }
      double alpha = 1.0;

      if( interpolationOption->GetNumberOfParameters() > 0 )
        {
        std::vector<double> s = parser->ConvertVector<double>( interpolationOption->GetParameter( 0 ) );
        if( s.size() == Dimension )
          {
          for( unsigned int d = 0; d < Dimension; d++ )
            {
            sigma[d] = s[d];
            }
          }
        else
          {
          for( unsigned int d = 0; d < Dimension; d++ )
            {
            sigma[d] = s[0];
            }
          }
        }
      if( interpolationOption->GetNumberOfParameters() > 1 )
        {
        alpha = parser->Convert<double>( interpolationOption->GetParameter( 1 ) );
        }
      gaussianInterpolator->SetParameters( sigma, alpha );
      interpolator = gaussianInterpolator;
      }
    else if( !std::strcmp( whichInterpolator.c_str(), "multilabel" ) )
      {
      double sigma[Dimension];
      for( unsigned int d = 0; d < Dimension; d++ )
        {
        sigma[d] = inputImages[0]->GetSpacing()[d];
        }
      double alpha = 4.0;

      if( interpolationOption->GetNumberOfParameters() > 0 )
        {
        std::vector<double> s = parser->ConvertVector<double>( interpolationOption->GetParameter( 0 ) );
        if( s.size() == Dimension )
          {
          for( unsigned int d = 0; d < Dimension; d++ )
            {
            sigma[d] = s[d];
            }
          }
        else
          {
          for( unsigned int d = 0; d < Dimension; d++ )
            {
            sigma[d] = s[0];
            }
          }
        }
      multiLabelInterpolator->SetParameters( sigma, alpha );
      interpolator = multiLabelInterpolator;
      }
    }

  /**
   * Default voxel value
   */
  PixelType defaultValue = 0;
  typename itk::ants::CommandLineParser::OptionType::Pointer defaultOption =
    parser->GetOption( "default-value" );
  if( defaultOption && defaultOption->GetNumberOfValues() > 0 )
    {
    defaultValue = parser->Convert<PixelType>( defaultOption->GetValue() );
    }
  antscout << "Default pixel value: " << defaultValue << std::endl;
  for( unsigned int n = 0; n < inputImages.size(); n++ )
    {
    typedef itk::ResampleImageFilter<ImageType, ImageType, RealType> ResamplerType;
    typename ResamplerType::Pointer resampleFilter = ResamplerType::New();
    resampleFilter->SetInput( inputImages[n] );
    resampleFilter->SetOutputParametersFromImage( referenceImage );
    resampleFilter->SetTransform( compositeTransform );
    resampleFilter->SetDefaultPixelValue( defaultValue );

    if( interpolator )
      {
      interpolator->SetInputImage( inputImages[n] );
      resampleFilter->SetInterpolator( interpolator );
      }
    else
      {
      // These interpolators need to be checked after instantiation of the resample filter.

      if( !std::strcmp( whichInterpolator.c_str(), "cosinewindowedsinc" ) )
        {
        cosineInterpolator->SetInputImage( inputImages[n] );
        resampleFilter->SetInterpolator( cosineInterpolator );
        }
      else if( !std::strcmp( whichInterpolator.c_str(), "hammingwindowedsinc" ) )
        {
        hammingInterpolator->SetInputImage( inputImages[n] );
        resampleFilter->SetInterpolator( hammingInterpolator );
        }
      else if( !std::strcmp( whichInterpolator.c_str(), "lanczoswindowedsinc" ) )
        {
        lanczosInterpolator->SetInputImage( inputImages[n] );
        resampleFilter->SetInterpolator( lanczosInterpolator );
        }
      else if( !std::strcmp( whichInterpolator.c_str(), "blackmanwindowedsinc" ) )
        {
        blackmanInterpolator->SetInputImage( inputImages[n] );
        resampleFilter->SetInterpolator( blackmanInterpolator );
        }
      else
        {
        resampleFilter->SetInterpolator( linearInterpolator );
        }
      }
    if( n == 0 )
      {
      antscout << "Interpolation type: " << resampleFilter->GetInterpolator()->GetNameOfClass() << std::endl;
      }

    resampleFilter->Update();

    outputImages.push_back( resampleFilter->GetOutput() );
    }

  /**
   * output
   */
  if( outputOption && outputOption->GetNumberOfValues() > 0 )
    {
    if( outputOption->GetNumberOfParameters( 0 ) > 1 &&
        parser->Convert<unsigned int>( outputOption->GetParameter( 0, 1 ) ) != 0 )
      {
      antscout << "Output composite transform displacement field: " << outputOption->GetParameter( 0, 0 ) << std::endl;

      typedef typename itk::TransformToDisplacementFieldSource<DisplacementFieldType> ConverterType;
      typename ConverterType::Pointer converter = ConverterType::New();
      converter->SetOutputParametersFromImage( referenceImage );
      converter->SetTransform( compositeTransform );

      typedef  itk::ImageFileWriter<DisplacementFieldType> DisplacementFieldWriterType;
      typename DisplacementFieldWriterType::Pointer displacementFieldWriter = DisplacementFieldWriterType::New();
      displacementFieldWriter->SetInput( converter->GetOutput() );
      displacementFieldWriter->SetFileName( ( outputOption->GetParameter( 0, 0 ) ).c_str() );
      displacementFieldWriter->Update();
      }
    else
      {
      std::string outputFileName = "";
      if( outputOption->GetNumberOfParameters( 0 ) > 1 &&
          parser->Convert<unsigned int>( outputOption->GetParameter( 0, 1 ) ) == 0 )
        {
        outputFileName = outputOption->GetParameter( 0, 0 );
        }
      else
        {
        outputFileName = outputOption->GetValue();
        }
      antscout << "Output warped image: " << outputFileName << std::endl;

      if( inputImageType == 1 )
        {
        if( outputImages.size() != Dimension )
          {
          antscout << "The number of output images does not match the number of vector components." << std::endl;
          return EXIT_FAILURE;
          }

        VectorType zeroVector( 0.0 );

        typename DisplacementFieldType::Pointer outputVectorImage = DisplacementFieldType::New();
        outputVectorImage->CopyInformation( referenceImage );
        outputVectorImage->SetRegions( referenceImage->GetRequestedRegion() );
        outputVectorImage->Allocate();
        outputVectorImage->FillBuffer( zeroVector );

        itk::ImageRegionIteratorWithIndex<DisplacementFieldType> It( outputVectorImage,
                                                                     outputVectorImage->GetRequestedRegion() );
        for( It.GoToBegin(); !It.IsAtEnd(); ++It )
          {
          VectorType vector = It.Get();
          typename DisplacementFieldType::IndexType index = It.GetIndex();
          for( unsigned int n = 0; n < Dimension; n++ )
            {
            vector.SetNthComponent( n, outputImages[n]->GetPixel( index ) );
            }
          It.Set( vector );
          }
        typedef  itk::ImageFileWriter<DisplacementFieldType> WriterType;
        typename WriterType::Pointer writer = WriterType::New();
        writer->SetInput( outputVectorImage );
        writer->SetFileName( ( outputFileName ).c_str() );
        writer->Update();
        }
      else if( inputImageType == 2 )
        {
        if( outputImages.size() != NumberOfTensorElements )
          {
          antscout << "The number of output images does not match the number of tensor elements." << std::endl;
          return EXIT_FAILURE;
          }

        TensorPixelType zeroTensor( 0.0 );

        typename TensorImageType::Pointer outputTensorImage = TensorImageType::New();
        outputTensorImage->CopyInformation( referenceImage );
        outputTensorImage->SetRegions( referenceImage->GetRequestedRegion() );
        outputTensorImage->Allocate();
        outputTensorImage->FillBuffer( zeroTensor );

        itk::ImageRegionIteratorWithIndex<TensorImageType> It( outputTensorImage,
                                                               outputTensorImage->GetRequestedRegion() );
        for( It.GoToBegin(); !It.IsAtEnd(); ++It )
          {
          TensorPixelType tensor = It.Get();
          typename TensorImageType::IndexType index = It.GetIndex();
          for( unsigned int n = 0; n < NumberOfTensorElements; n++ )
            {
            tensor.SetNthComponent( n, outputImages[n]->GetPixel( index ) );
            }
          It.Set( tensor );
          }

        WriteTensorImage<TensorImageType>( outputTensorImage, ( outputFileName ).c_str(), true );
        }
      else
        {
        typedef  itk::ImageFileWriter<ImageType> WriterType;
        typename WriterType::Pointer writer = WriterType::New();
        writer->SetInput( outputImages[0] );
        writer->SetFileName( ( outputFileName ).c_str() );
        writer->Update();
        }
      }
    }

  return EXIT_SUCCESS;
}

static void InitializeCommandLineOptions( itk::ants::CommandLineParser *parser )
{
  typedef itk::ants::CommandLineParser::OptionType OptionType;

    {
    std::string description =
      std::string( "This option forces the image to be treated as a specified-" )
      + std::string( "dimensional image.  If not specified, antsWarp tries to " )
      + std::string( "infer the dimensionality from the input image." );

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "dimensionality" );
    option->SetShortName( 'd' );
    option->SetUsageOption( 0, "2/3" );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description =
      std::string( "Option specifying the input image type of scalar (default), " )
      + std::string( "vector, or tensor." );

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "input-image-type" );
    option->SetShortName( 'e' );
    option->SetUsageOption( 0, "0/1/2 " );
    option->SetUsageOption( 1, "scalar/vector/tensor " );
    option->AddValue( std::string( "0" ) );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description =
      std::string( "Currently, the only input objects supported are image " )
      + std::string( "objects.  However, the current framework allows for " )
      + std::string( "warping of other objects such as meshes and point sets. ");

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "input" );
    option->SetShortName( 'i' );
    option->SetUsageOption( 0, "inputFileName" );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description =
      std::string( "For warping input images, the reference image defines the " )
      + std::string( "spacing, origin, size, and direction of the output warped " )
      + std::string( "image. ");

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "reference-image" );
    option->SetShortName( 'r' );
    option->SetUsageOption( 0, "imageFileName" );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description =
      std::string( "One can either output the warped image or, if the boolean " )
      + std::string( "is set, one can print out the displacement field based on the" )
      + std::string( "composite transform and the reference image." );

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "output" );
    option->SetShortName( 'o' );
    option->SetUsageOption( 0, "warpedOutputFileName" );
    option->SetUsageOption( 1, "[compositeDisplacementField,<printOutCompositeWarpFile=0>]" );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description =
      std::string( "Several interpolation options are available in ITK. " )
      + std::string( "These have all been made available." );

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "interpolation" );
    option->SetShortName( 'n' );
    option->SetUsageOption( 0, "Linear" );
    option->SetUsageOption( 1, "NearestNeighbor" );
    option->SetUsageOption( 2, "MultiLabel[<sigma=imageSpacing>,<alpha=4.0>]" );
    option->SetUsageOption( 3, "Gaussian[<sigma=imageSpacing>,<alpha=1.0>]" );
    option->SetUsageOption( 4, "BSpline[<order=3>]" );
    option->SetUsageOption( 5, "CosineWindowedSinc" );
    option->SetUsageOption( 6, "WelchWindowedSinc" );
    option->SetUsageOption( 7, "HammingWindowedSinc" );
    option->SetUsageOption( 8, "LanczosWindowedSinc" );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description =
      std::string( "Several transform options are supported including all " )
      + std::string( "those defined in the ITK library in addition to " )
      + std::string( "a deformation field transform.  The ordering of " )
      + std::string( "the transformations follows the ordering specified " )
      + std::string( "on the command line.  An identity transform is pushed " )
      + std::string( "onto the transformation stack. Each new transform " )
      + std::string( "encountered on the command line is also pushed onto " )
      + std::string( "the transformation stack. Then, to warp the input object, " )
      + std::string( "each point comprising the input object is warped first " )
      + std::string( "according to the last transform pushed onto the stack " )
      + std::string( "followed by the second to last transform, etc. until " )
      + std::string( "the last transform encountered which is the identity " )
      + std::string( "transform. " )
      + std::string( "Also, it should be noted that the inverse transform can " )
      + std::string( "be accommodated with the usual caveat that such an inverse " )
      + std::string( "must be defined by the specified transform class " );

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "transform" );
    option->SetShortName( 't' );
    option->SetUsageOption( 0, "transformFileName" );
    option->SetUsageOption( 1, "[transformFileName,useInverse]" );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description =
      std::string( "Default voxel value to be used with input images only. " )
      + std::string( "Specifies the voxel value when the input point maps outside " )
      + std::string( "the output domain" );

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "default-value" );
    option->SetShortName( 'v' );
    option->SetUsageOption( 0, "value" );
    option->SetDescription( description );
    parser->AddOption( option );
    }

    {
    std::string description = std::string( "Print the help menu (short version)." );

    OptionType::Pointer option = OptionType::New();
    option->SetShortName( 'h' );
    option->SetDescription( description );
    option->AddValue( std::string( "0" ) );
    parser->AddOption( option );
    }

    {
    std::string description = std::string( "Print the help menu." );

    OptionType::Pointer option = OptionType::New();
    option->SetLongName( "help" );
    option->SetDescription( description );
    option->AddValue( std::string( "0" ) );
    parser->AddOption( option );
    }
}

// entry point for the library; parameter 'args' is equivalent to 'argv' in (argc,argv) of commandline parameters to
// 'main()'
int antsApplyTransforms( std::vector<std::string> args, std::ostream* out_stream = NULL )
{
  // put the arguments coming in as 'args' into standard (argc,argv) format;
  // 'args' doesn't have the command name as first, argument, so add it manually;
  // 'args' may have adjacent arguments concatenated into one argument,
  // which the parser should handle
  args.insert( args.begin(), "antsApplyTransforms" );
  std::remove( args.begin(), args.end(), std::string( "" ) );
  std::remove( args.begin(), args.end(), std::string( "" ) );
  int     argc = args.size();
  char* * argv = new char *[args.size() + 1];
  for( unsigned int i = 0; i < args.size(); ++i )
    {
    // allocate space for the string plus a null character
    argv[i] = new char[args[i].length() + 1];
    std::strncpy( argv[i], args[i].c_str(), args[i].length() );
    // place the null character in the end
    argv[i][args[i].length()] = '\0';
    }
  argv[argc] = 0;
  // class to automatically cleanup argv upon destruction
  class Cleanup_argv
  {
public:
    Cleanup_argv( char* * argv_, int argc_plus_one_ ) : argv( argv_ ), argc_plus_one( argc_plus_one_ )
    {
    }

    ~Cleanup_argv()
    {
      for( unsigned int i = 0; i < argc_plus_one; ++i )
        {
        delete[] argv[i];
        }
      delete[] argv;
    }

private:
    char* *      argv;
    unsigned int argc_plus_one;
  };
  Cleanup_argv cleanup_argv( argv, argc + 1 );

  antscout->set_stream( out_stream );

  itk::ants::CommandLineParser::Pointer parser =
    itk::ants::CommandLineParser::New();

  parser->SetCommand( argv[0] );

  std::string commandDescription =
    std::string( "antsApplyTransforms, applied to an input image, transforms it " )
    + std::string( "according to a reference image and a transform " )
    + std::string( "(or a set of transforms)." );

  parser->SetCommandDescription( commandDescription );
  InitializeCommandLineOptions( parser );

  parser->Parse( argc, argv );

  if( argc < 2 || ( parser->GetOption( "help" ) &&
                    ( parser->Convert<bool>( parser->GetOption( "help" )->GetValue() ) ) ) )
    {
    parser->PrintMenu( antscout, 5, false );
    if( argc < 2 )
      {
      return EXIT_FAILURE;
      }
    return EXIT_SUCCESS;
    }
  else if( parser->GetOption( 'h' ) &&
           ( parser->Convert<bool>( parser->GetOption( 'h' )->GetValue() ) ) )
    {
    parser->PrintMenu( antscout, 5, true );
    return EXIT_SUCCESS;
    }

  // Read in the first intensity image to get the image dimension.
  std::string filename;

  itk::ants::CommandLineParser::OptionType::Pointer inputOption =
    parser->GetOption( "reference-image" );
  if( inputOption && inputOption->GetNumberOfValues() > 0 )
    {
    if( inputOption->GetNumberOfParameters( 0 ) > 0 )
      {
      filename = inputOption->GetParameter( 0, 0 );
      }
    else
      {
      filename = inputOption->GetValue( 0 );
      }
    }
  else
    {
    antscout << "No reference image was specified." << std::endl;
    return EXIT_FAILURE;
    }

  itk::ants::CommandLineParser::OptionType::Pointer inputImageTypeOption =
    parser->GetOption( "input-image-type" );

  unsigned int dimension = 3;

  itk::ImageIOBase::Pointer imageIO = itk::ImageIOFactory::CreateImageIO(
      filename.c_str(), itk::ImageIOFactory::ReadMode );
  dimension = imageIO->GetNumberOfDimensions();

  itk::ants::CommandLineParser::OptionType::Pointer dimOption =
    parser->GetOption( "dimensionality" );
  if( dimOption && dimOption->GetNumberOfValues() > 0 )
    {
    dimension = parser->Convert<unsigned int>( dimOption->GetValue() );
    }

  switch( dimension )
    {
    case 2:
      {
      if( inputImageTypeOption )
        {
        std::string inputImageType = inputImageTypeOption->GetValue();

        if( !std::strcmp( inputImageType.c_str(), "scalar" ) || !std::strcmp( inputImageType.c_str(), "0" ) )
          {
          antsApplyTransforms<2>( parser, 0 );
          }
        else if( !std::strcmp( inputImageType.c_str(), "vector" ) || !std::strcmp( inputImageType.c_str(), "1" ) )
          {
          antsApplyTransforms<2>( parser, 1 );
          }
        else if( !std::strcmp( inputImageType.c_str(), "tensor" ) || !std::strcmp( inputImageType.c_str(), "2" ) )
          {
          antscout << "antsApplyTransforms is not implemented for 2-D tensor images." << std::endl;
          }
        else
          {
          antscout << "Unrecognized input image type (cf --input-image-type option)." << std::endl;
          return EXIT_FAILURE;
          }
        }
      else
        {
        antsApplyTransforms<2>( parser, 0 );
        }
      }
      break;
    case 3:
      {
      if( inputImageTypeOption )
        {
        std::string inputImageType = inputImageTypeOption->GetValue();

        if( !std::strcmp( inputImageType.c_str(), "scalar" ) || !std::strcmp( inputImageType.c_str(), "0" ) )
          {
          antsApplyTransforms<3>( parser, 0 );
          }
        else if( !std::strcmp( inputImageType.c_str(), "vector" ) || !std::strcmp( inputImageType.c_str(), "1" ) )
          {
          antsApplyTransforms<3>( parser, 1 );
          }
        else if( !std::strcmp( inputImageType.c_str(), "tensor" ) || !std::strcmp( inputImageType.c_str(), "2" ) )
          {
          antsApplyTransforms<3>( parser, 2 );
          }
        else
          {
          antscout << "Unrecognized input image type (cf --input-image-type option)." << std::endl;
          return EXIT_FAILURE;
          }
        }
      else
        {
        antsApplyTransforms<3>( parser, 0 );
        }
      }
      break;
    case 4:
      {
      if( inputImageTypeOption )
        {
        std::string inputImageType = inputImageTypeOption->GetValue();

        if( !std::strcmp( inputImageType.c_str(), "scalar" ) || !std::strcmp( inputImageType.c_str(), "0" ) )
          {
          antsApplyTransforms<4>( parser, 0 );
          }
        else if( !std::strcmp( inputImageType.c_str(), "vector" ) || !std::strcmp( inputImageType.c_str(), "1" ) )
          {
          antsApplyTransforms<4>( parser, 1 );
          }
        else if( !std::strcmp( inputImageType.c_str(), "tensor" ) || !std::strcmp( inputImageType.c_str(), "2" ) )
          {
          antscout << "antsApplyTransforms is not implemented for 4-D tensor images." << std::endl;
          }
        else
          {
          antscout << "Unrecognized input image type (cf --input-image-type option)." << std::endl;
          return EXIT_FAILURE;
          }
        }
      else
        {
        antsApplyTransforms<3>( parser, 0 );
        }
      }
      break;
    default:
      antscout << "Unsupported dimension" << std::endl;
      return EXIT_FAILURE;
    }
  return EXIT_SUCCESS;
}
} // namespace ants
