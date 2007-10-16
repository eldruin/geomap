import vigra

def tensor2Gradient(tensor):
    """rebuild vectors in dir. of large eigenvectors with lengths sqrt(trace)"""
    return vigra.transformImage(
        vigra.tensorEigenRepresentation(tensor), "\l e: sqrt(e[0]*e[0]+e[1]*e[1])*Vector(cos(-e[2]), sin(-e[2]))", {})

def gaussianHessian(image, sigma):
    """Return three-band tensor image containing the second
    derivatives calculated with gaussianDerivativeKernels."""
    result = vigra.PythonImage(3, image.size())
    for i in range(3):
        kernel = vigra.gaussianDerivativeKernel(sigma, 2-i, sigma, i)
        result[i].copyValues(vigra.convolveImage(image, kernel))
    return result

def gm2Gradient(gm, sigma):
    """'reverse-engineer' gradient vector image from scalar image
    via Hessian from Gaussian derivative filters of given scale"""
    
    # create vectors in direction of negative curvature:
    er = vigra.tensorEigenRepresentation(
        vigra.gaussianHessian(gm, sigma))
    return vigra.transformImage(
        er, gm, "\l e,i: i*Vector(-sin(e[2]), cos(e[2]))")

def colorGradient(img, scale, sqrt = True):
    """Calculate Gaussian color gradient.  Calculates sum of Gaussian
    gradient tensors in all single bands, and returns pair of (gm,
    grad) where gm is the square root of the trace of the color
    gradient tensor and grad is a 2-band image with the large
    eigenvectors of the appropriate sqrt(gm2) lengths.

    If `sqrt` is set to False, returns (gm2, grad) instead, i.e. does
    not apply sqrt to the first image (slight optimization if you
    don't need the gm image)."""
    
    bandTensors = [
        vigra.vectorToTensor(
        vigra.gaussianGradientAsVector(img.subImage(i), scale))
        for i in range(img.bands())]
    colorTensor = sum(bandTensors[1:], bandTensors[0])
    # gm2 := sum of squared magnitudes of grad. in each channel
    gm2 = vigra.tensorTrace(colorTensor)
    # rebuild vector in dir. of large eigenvector with length sqrt(gm2):
    grad = vigra.transformImage(
        vigra.tensorEigenRepresentation(colorTensor), gm2,
        "\l e, mag2: sqrt(mag2)*Vector(cos(-e[2]), sin(-e[2]))", {})
    if sqrt:
        gm = vigra.transformImage(gm2, "\l x: sqrt(x)")
        return gm, grad
    return gm2, grad

def gaussianGradient(img, scale, sqrt = True):
    if img.bands() > 1:
        return colorGradient(img, scale, sqrt)

    grad = vigra.gaussianGradientAsVector(img, scale)
    if sqrt:
        gm = vigra.transformImage(grad, "\l x: norm(x)")
    else:
        gm = vigra.transformImage(grad, "\l x: squaredNorm(x)")
    return gm, grad

def structureTensorFromGradient(grad, scale):
    return vigra.gaussianSmoothing(vigra.vectorToTensor(grad), scale)
